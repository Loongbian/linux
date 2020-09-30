// SPDX-License-Identifier: GPL-2.0-only
/*******************************************************************************
  This contains the functions to handle the pci driver.

  Copyright (C) 2011-2012  Vayavya Labs Pvt Ltd


  Author: Rayagond Kokatanur <rayagond@vayavyalabs.com>
  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_net.h>
#include <linux/pci.h>

#include "stmmac.h"
#include "stmmac_platform.h"

static void loongson_dwmac_hwconfig(struct plat_stmmacenet_data *plat)
{
	plat->clk_csr = 2;	/* clk_csr_i = 20-35MHz & MDC = clk_csr_i/16 */
	plat->has_gmac = 1;
	plat->enh_desc = 1;

	/* Set default value for multicast hash bins */
	plat->multicast_filter_bins = 256;

	/* Set default value for unicast filter entries */
	plat->unicast_filter_entries = 1;

	/* Set the maxmtu to a default of JUMBO_LEN */
	plat->maxmtu = JUMBO_LEN;

	/* Set default number of RX and TX queues to use */
	plat->tx_queues_to_use = 1;
	plat->rx_queues_to_use = 1;

	/* Disable Priority config by default */
	plat->tx_queues_cfg[0].use_prio = false;
	plat->rx_queues_cfg[0].use_prio = false;

	/* Disable RX queues routing by default */
	plat->rx_queues_cfg[0].pkt_route = 0x0;

	plat->dma_cfg->pbl = 32;
	plat->dma_cfg->pblx8 = true;

	plat->axi->axi_wr_osr_lmt = 1;
	plat->axi->axi_rd_osr_lmt = 1;
	/* Hardware support 256 burst only */
	plat->axi->axi_blen[0] = 256;
}

static int loongson_dwmac_of_probe(struct device *dev,
				   struct plat_stmmacenet_data *plat,
				   const char **mac)
{
	struct device_node *np = dev->of_node;

	*mac = of_get_mac_address(np);
	if (IS_ERR(*mac)) {
		if (PTR_ERR(*mac) == -EPROBE_DEFER)
			return PTR_ERR(*mac);

		*mac = NULL;
	}

	plat->phy_interface = device_get_phy_mode(dev);
	if (plat->phy_interface < 0)
		return -ENODEV;

	plat->interface = stmmac_of_get_mac_mode(np);
	if (plat->interface < 0)
		plat->interface = plat->phy_interface;

	/* PHYLINK automatically parses the phy-handle property */
	plat->phylink_node = np;

	/* Get max speed of operation from device tree */
	if (of_property_read_u32(np, "max-speed", &plat->max_speed))
		plat->max_speed = -1;

	plat->bus_id = of_alias_get_id(np, "ethernet");
	if (plat->bus_id < 0)
		plat->bus_id = 0;

	return stmmac_dt_phy(plat, np, dev);
}

static int dwmac_loongson_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
    struct device_node *np = pdev->dev.of_node;
	struct plat_stmmacenet_data *plat;
	struct stmmac_resources res;
	int i;
	int ret;

	if (!np) {
		pr_info("dwmac_loongson_pci: No OF node\n");
		return -ENODEV;
	}

	if (!of_device_is_compatible(np, "loongson,pci-gmac")) {
		pr_info("dwmac_loongson_pci: Incompatible OF node\n");
		return -ENODEV;
	}

	plat = devm_kzalloc(&pdev->dev, sizeof(*plat), GFP_KERNEL);
	if (!plat)
		return -ENOMEM;

	plat->dma_cfg = devm_kzalloc(&pdev->dev, sizeof(*plat->dma_cfg),
				     GFP_KERNEL);
	if (!plat->dma_cfg)
		return -ENOMEM;

	plat->axi = devm_kzalloc(&pdev->dev, sizeof(*plat->axi), GFP_KERNEL);
	if (!plat->axi)
		return -ENOMEM;

	loongson_dwmac_hwconfig(plat);

	ret = loongson_dwmac_of_probe(&pdev->dev, plat, &res.mac);
	if (ret) {
		dev_err(&pdev->dev, "OF probe failed\n");
	}

	res.irq = of_irq_get_byname(np, "macirq");
	if (res.irq < 0) {
		dev_err(&pdev->dev, "IRQ macirq not found\n");
		ret = -ENODEV;
		goto free_plat;
	}

	res.lpi_irq = of_irq_get_byname(np, "eth_lpi");
	if (res.lpi_irq < 0) {
		dev_err(&pdev->dev, "IRQ eth_lpi not found\n");
		ret = -ENODEV;
		goto free_plat;
	}

	res.wol_irq = of_irq_get_byname(np, "eth_wake_irq");
	if (res.wol_irq < 0) {
		dev_info(&pdev->dev, "IRQ eth_wake_irq not found, using macirq\n");
		res.wol_irq = res.irq;
	}

	/* Enable pci device */
	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "%s: ERROR: failed to enable device\n",
			__func__);
		goto free_plat;
	}

	/* Get the base address of device */
	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
		ret = pcim_iomap_regions(pdev, BIT(i), pci_name(pdev));
		if (ret)
			goto free_plat;
		break;
	}

	pci_set_master(pdev);

	res.addr = pcim_iomap_table(pdev)[i];

	return stmmac_dvr_probe(&pdev->dev, plat, &res);

free_plat:
	kfree(plat->dma_cfg);
	kfree(plat->axi);
	kfree(plat->mdio_bus_data);
	kfree(plat);
	return ret;
}

/**
 * dwmac_loongson_pci_remove
 *
 * @pdev: platform device pointer
 * Description: this function calls the main to free the net resources
 * and releases the PCI resources.
 */
static void dwmac_loongson_pci_remove(struct pci_dev *pdev)
{
	int i;

	stmmac_dvr_remove(&pdev->dev);

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
		pcim_iounmap_regions(pdev, BIT(i));
		break;
	}

	pci_disable_device(pdev);
}

static int __maybe_unused dwmac_loongson_pci_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	ret = stmmac_suspend(dev);
	if (ret)
		return ret;

	ret = pci_save_state(pdev);
	if (ret)
		return ret;

	pci_disable_device(pdev);
	pci_wake_from_d3(pdev, true);
	return 0;
}

static int __maybe_unused dwmac_loongson_pci_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	pci_restore_state(pdev);
	pci_set_power_state(pdev, PCI_D0);

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	pci_set_master(pdev);

	return stmmac_resume(dev);
}

static SIMPLE_DEV_PM_OPS(dwmac_loongson_pm_ops, dwmac_loongson_pci_suspend, dwmac_loongson_pci_resume);


static const struct pci_device_id dwmac_loongson_id_table[] = {
	{ PCI_VDEVICE(LOONGSON, 0x7a03) },
	{}
};

MODULE_DEVICE_TABLE(pci, stmmac_id_table);

static struct pci_driver dwmac_loongson_pci_driver = {
	.name = "Loongson-dwmac-PCI",
	.id_table = dwmac_loongson_id_table,
	.probe = dwmac_loongson_pci_probe,
	.remove = dwmac_loongson_pci_remove,
	.driver         = {
		.pm     = &dwmac_loongson_pm_ops,
	},
};

module_pci_driver(dwmac_loongson_pci_driver);

MODULE_DESCRIPTION("Loongson GMAC PCI driver");
MODULE_AUTHOR("Jiaxun Yang <jiaxun.yang@flygoat.com>");
MODULE_LICENSE("GPL");
