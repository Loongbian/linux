/*
 *
 *  hda_loongson.c - Implementation of primary alsa driver code base
 *                for Intel HD Audio.
 *
 *  Copyright (c) 2004 Intel Corporation. All rights reserved.
 *
 *  Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 *                     PeiSen Hou <pshou@realtek.com.tw>
 *
 *  Copyright (c) 2014 Huacai Chen <chenhc@lemote.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *  CONTACTS:
 *
 *  Matt Jared		matt.jared@intel.com
 *  Andy Kopp		andy.kopp@intel.com
 *  Dan Kogan		dan.d.kogan@intel.com
 *
 *  CHANGES:
 *
 *  2004.12.01	Major rewrite by tiwai, merged the work of pshou
 *
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/clocksource.h>
#include <linux/time.h>
#include <linux/completion.h>
#include <linux/pci.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <linux/firmware.h>
#include <sound/hda_codec.h>
//#include "hda_codec.h"
#include "hda_controller.h"

#ifdef CONFIG_CPU_LOONGSON3
#include <boot_param.h>
#include <loongson-pch.h>
#endif

/* macros for convenience. */
#define platform_resource_start(dev,bar)   ((dev)->resource[(bar)].start)
#define platform_resource_end(dev,bar)     ((dev)->resource[(bar)].end)
#define platform_resource_flags(dev,bar)   ((dev)->resource[(bar)].flags)
#define platform_resource_len(dev,bar) \
        ((platform_resource_start((dev),(bar)) == 0 &&       \
          platform_resource_end((dev),(bar)) ==              \
          platform_resource_start((dev),(bar))) ? 0 :        \
                                                        \
         (platform_resource_end((dev),(bar)) -               \
          platform_resource_start((dev),(bar)) + 1))

#define ICH6_NUM_CAPTURE	4
#define ICH6_NUM_PLAYBACK	4

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;
static char *model[SNDRV_CARDS];
static int bdl_pos_adj[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] = -1};
static int probe_only[SNDRV_CARDS];
static int jackpoll_ms[SNDRV_CARDS] = {1000};
static bool single_cmd;
static int enable_msi = -1;
#ifdef CONFIG_SND_HDA_INPUT_BEEP
static bool beep_mode[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] =
					CONFIG_SND_HDA_INPUT_BEEP_MODE};
#endif

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Intel HD audio interface.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Intel HD audio interface.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Intel HD audio interface.");
module_param_array(model, charp, NULL, 0444);
MODULE_PARM_DESC(model, "Use the given board model.");
module_param_array(bdl_pos_adj, int, NULL, 0644);
MODULE_PARM_DESC(bdl_pos_adj, "BDL position adjustment offset.");
module_param_array(probe_only, int, NULL, 0444);
MODULE_PARM_DESC(probe_only, "Only probing and no codec initialization.");
module_param_array(jackpoll_ms, int, NULL, 0444);
MODULE_PARM_DESC(jackpoll_ms, "Ms between polling for jack events (default = 0, using unsol events only)");
module_param(single_cmd, bool, 0444);
MODULE_PARM_DESC(single_cmd, "Use single command to communicate with codecs "
		 "(for debugging only).");
module_param(enable_msi, bint, 0444);
MODULE_PARM_DESC(enable_msi, "Enable Message Signaled Interrupt (MSI)");
#ifdef CONFIG_SND_HDA_INPUT_BEEP
module_param_array(beep_mode, bool, NULL, 0444);
MODULE_PARM_DESC(beep_mode, "Select HDA Beep registration mode "
			    "(0=off, 1=on) (default=1).");
#endif

#ifdef CONFIG_PM
static int param_set_xint(const char *val, const struct kernel_param *kp);
static struct kernel_param_ops param_ops_xint = {
	.set = param_set_xint,
	.get = param_get_int,
};
#define param_check_xint param_check_int

static int power_save = CONFIG_SND_HDA_POWER_SAVE_DEFAULT;
module_param(power_save, xint, 0644);
MODULE_PARM_DESC(power_save, "Automatic power-saving timeout "
		 "(in second, 0 = disable).");

/* reset the HD-audio controller in power save mode.
 * this may give more power-saving, but will take longer time to
 * wake up.
 */
static bool power_save_controller = 1;
module_param(power_save_controller, bool, 0644);
MODULE_PARM_DESC(power_save_controller, "Reset controller in power save mode.");
#else
#define power_save	0
#endif /* CONFIG_PM */
static int use_pci_probe = 0;
static int align_buffer_size = -1;
module_param(align_buffer_size, bint, 0644);
MODULE_PARM_DESC(align_buffer_size,
		"Force buffer and period sizes to be multiple of 128 bytes.");

MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Intel, ICH6},"
			 "{Intel, ICH6M},"
			 "{Intel, ICH7},"
			 "{Intel, ICH8},"
			 "{Intel, ICH9},"
			 "{Intel, ICH10}}");
MODULE_DESCRIPTION("Loongson HDA driver");

/* driver types */
enum {
	AZX_DRIVER_ICH,
	AZX_NUM_DRIVERS, /* keep this as last entry */
};

static char *driver_short_names[] = {
	[AZX_DRIVER_ICH] = "HD-Audio Loongson",
};

struct hda_loongson {
	struct azx chip;

	/* for pending irqs */
	struct work_struct irq_pending_work;

	/* sync probing */
	struct completion probe_wait;
	struct work_struct probe_work;

	/* card list (for power_save trigger) */
	struct list_head list;

	/* extra flags */
	unsigned int irq_pending_warned:1;
	unsigned int probe_continued:1;
};

static int azx_acquire_irq(struct azx *chip, int do_disconnect);

static int azx_position_ok(struct azx *chip, struct azx_dev *azx_dev);

/*
 * HDA controller ops.
 */

static u32 loongson_azx_readl(u32 __iomem *addr)
{
	return readl(addr);
}

static void loongson_azx_writel(u32 val, u32 __iomem *addr)
{
	writel(val, addr);
}

static u16 loongson_azx_readw(u16 __iomem *addr)
{
	return readw(addr);
}

static void loongson_azx_writew(u16 val, u16 __iomem *addr)
{
	writew(val, addr);
}

static u8 loongson_azx_readb(u8 __iomem *addr)
{
	return readb(addr);
}

static void loongson_azx_writeb(u8 val, u8 __iomem *addr)
{
	writeb(val, addr);
}
/* called from IRQ */
static int azx_position_check(struct azx *chip, struct azx_dev *azx_dev)
{
	struct hda_loongson *hda = container_of(chip, struct hda_loongson, chip);
	int ok;

	ok = azx_position_ok(chip, azx_dev);
	if (ok == 1) {
		azx_dev->irq_pending = 0;
		return ok;
	} else if (ok == 0) {
		/* bogus IRQ, process it later */
		azx_dev->irq_pending = 1;
		schedule_work(&hda->irq_pending_work);
	}
	return 0;
}

/*
 * Check whether the current DMA position is acceptable for updating
 * periods.  Returns non-zero if it's OK.
 *
 * Many HD-audio controllers appear pretty inaccurate about
 * the update-IRQ timing.  The IRQ is issued before actually the
 * data is processed.  So, we need to process it afterwords in a
 * workqueue.
 */
static int azx_position_ok(struct azx *chip, struct azx_dev *azx_dev)
{
	return 1; /* OK, it's fine */
}

/*
 * The work for pending PCM period updates.
 */
static void azx_irq_pending_work(struct work_struct *work)
{
	struct hda_loongson *hda = container_of(work, struct hda_loongson, irq_pending_work);
	struct azx *chip = &hda->chip;
	struct hdac_bus *bus = azx_bus(chip);
	struct hdac_stream *s;
	int pending, ok;

	if (!hda->irq_pending_warned) {
		dev_info(chip->card->dev,
			 "IRQ timing workaround is activated for card #%d. Suggest a bigger bdl_pos_adj.\n",
			 chip->card->number);
		hda->irq_pending_warned = 1;
	}

	for (;;) {
		pending = 0;
		spin_lock_irq(&bus->reg_lock);
		list_for_each_entry(s, &bus->stream_list, list) {
			struct azx_dev *azx_dev = stream_to_azx_dev(s);
			if (!azx_dev->irq_pending ||
			    !s->substream ||
			    !s->running)
				continue;
			ok = azx_position_ok(chip, azx_dev);
			if (ok > 0) {
				azx_dev->irq_pending = 0;
				spin_unlock(&bus->reg_lock);
				snd_pcm_period_elapsed(s->substream);
				spin_lock(&bus->reg_lock);
			} else if (ok < 0) {
				pending = 0;	/* too early */
			} else
				pending++;
		}
		spin_unlock_irq(&bus->reg_lock);
		if (!pending)
			return;
		msleep(1);
	}
}

/* clear irq_pending flags and assure no on-going workq */
static void azx_clear_irq_pending(struct azx *chip)
{
	struct hdac_bus *bus = azx_bus(chip);
	struct hdac_stream *s;

	spin_lock_irq(&bus->reg_lock);
	list_for_each_entry(s, &bus->stream_list, list) {
		struct azx_dev *azx_dev = stream_to_azx_dev(s);
		azx_dev->irq_pending = 0;
	}
	spin_unlock_irq(&bus->reg_lock);
}

static int azx_acquire_irq(struct azx *chip, int do_disconnect)
{
	struct hdac_bus *bus = azx_bus(chip);
	int irq;
	
	if (use_pci_probe)
		irq = chip->pci->irq;
	else
		irq = platform_get_irq(to_platform_device(chip->card->dev), 0);

	if (request_irq(irq, azx_interrupt, chip->msi ? 0 : IRQF_SHARED,
			KBUILD_MODNAME, chip)) {
		dev_err(chip->card->dev,
			"unable to grab IRQ %d, disabling device\n", irq);
		if (do_disconnect)
			snd_card_disconnect(chip->card);
		return -1;
	}
	bus->irq = irq;
	return 0;
}

#ifdef CONFIG_PM
static DEFINE_MUTEX(card_list_lock);
static LIST_HEAD(card_list);

static void azx_add_card_list(struct azx *chip)
{
	struct hda_loongson *hda = container_of(chip, struct hda_loongson, chip);
	mutex_lock(&card_list_lock);
	list_add(&hda->list, &card_list);
	mutex_unlock(&card_list_lock);
}

static void azx_del_card_list(struct azx *chip)
{
	struct hda_loongson *hda = container_of(chip, struct hda_loongson, chip);
	mutex_lock(&card_list_lock);
	list_del_init(&hda->list);
	mutex_unlock(&card_list_lock);
}

/* trigger power-save check at writing parameter */
static int param_set_xint(const char *val, const struct kernel_param *kp)
{
	struct hda_loongson *hda;
	struct azx *chip;
	int prev = power_save;
	int ret = param_set_int(val, kp);

	if (ret || prev == power_save)
		return ret;

	mutex_lock(&card_list_lock);
	list_for_each_entry(hda, &card_list, list) {
		chip = &hda->chip;
		if (!hda->probe_continued || chip->disabled)
			continue;
		snd_hda_set_power_save(&chip->bus, power_save * 1000);
	}
	mutex_unlock(&card_list_lock);
	return 0;
}
#else
#define azx_add_card_list(chip) /* NOP */
#define azx_del_card_list(chip) /* NOP */
#endif /* CONFIG_PM */

#if defined(CONFIG_PM_SLEEP)
/*
 * power management
 */
static int azx_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;
	struct hdac_bus *bus;

	if (chip->disabled)
		return 0;

	bus = azx_bus(chip);
	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	azx_clear_irq_pending(chip);
	azx_stop_chip(chip);
	azx_enter_link_reset(chip);
	if (bus->irq >= 0) {
		free_irq(bus->irq, chip);
		bus->irq = -1;
	}
	return 0;
}

static int azx_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;

	if (chip->disabled)
		return 0;

	chip->msi = 0;
	if (azx_acquire_irq(chip, 1) < 0)
		return -EIO;

	azx_init_chip(chip, true);

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM
static int azx_runtime_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;

	if (chip->disabled)
		return 0;

	if (!azx_has_pm_runtime(chip))
		return 0;

	/* enable controller wake up event */
	azx_writew(chip, WAKEEN, azx_readw(chip, WAKEEN) |
		  STATESTS_INT_MASK);

	azx_stop_chip(chip);
	azx_enter_link_reset(chip);
	azx_clear_irq_pending(chip);
	return 0;
}

static int azx_runtime_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;
	struct hda_codec *codec;
	int status;

	if (chip->disabled)
		return 0;

	if (!azx_has_pm_runtime(chip))
		return 0;

	/* Read STATESTS before controller reset */
	status = azx_readw(chip, STATESTS);

	azx_init_chip(chip, true);

	if (status) {
		list_for_each_codec(codec, &chip->bus)
			if (status & (1 << codec->addr))
				schedule_delayed_work(&codec->jackpoll_work,
						      codec->jackpoll_interval);
	}

	/* disable controller Wake Up event*/
	azx_writew(chip, WAKEEN, azx_readw(chip, WAKEEN) &
			~STATESTS_INT_MASK);

	return 0;
}

static int azx_runtime_idle(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;

	if (chip->disabled)
		return 0;

	if (!power_save_controller || !azx_has_pm_runtime(chip) ||
	    azx_bus(chip)->codec_powered)
		return -EBUSY;

	return 0;
}

#endif /* CONFIG_PM */

#ifdef CONFIG_PM
static const struct dev_pm_ops azx_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(azx_suspend, azx_resume)
	SET_RUNTIME_PM_OPS(azx_runtime_suspend, azx_runtime_resume, azx_runtime_idle)
};

#define AZX_PM_OPS	&azx_pm
#else
#define AZX_PM_OPS	NULL
#endif /* CONFIG_PM */

static int azx_probe_continue(struct azx *chip);

/*
 * destructor
 */
static int azx_free(struct azx *chip)
{
	struct device *snddev = chip->card->dev;
	struct hda_loongson *hda = container_of(chip, struct hda_loongson, chip);
	struct hdac_bus *bus = azx_bus(chip);

	if (azx_has_pm_runtime(chip) && chip->running)
		pm_runtime_get_noresume(snddev);

	azx_del_card_list(chip);

	complete_all(&hda->probe_wait);

	if (bus->chip_init) {
		azx_clear_irq_pending(chip);
		azx_stop_all_streams(chip);
		azx_stop_chip(chip);
	}

	if (bus->irq >= 0)
		free_irq(bus->irq, (void*)chip);
	if (bus->remap_addr)
		iounmap(bus->remap_addr);

	azx_free_stream_pages(chip);
	azx_free_streams(chip);
	snd_hdac_bus_exit(bus);
	kfree(hda);

	return 0;
}

static int azx_dev_disconnect(struct snd_device *device)
{
	struct azx *chip = device->device_data;

	chip->bus.shutdown = 1;
	return 0;
}

static int azx_dev_free(struct snd_device *device)
{
	return azx_free(device->device_data);
}

/*
 * constructor
 */

static const struct hda_controller_ops loongson_hda_ops;
static void azx_probe_work(struct work_struct *work)
{
	struct hda_loongson *hda = container_of(work, struct hda_loongson, probe_work);
	azx_probe_continue(&hda->chip);
}

static int azx_create(struct snd_card *card, struct pci_dev *pci, int dev, 
				unsigned int driver_caps, struct azx **rchip)
{
	static struct snd_device_ops ops = {
		.dev_disconnect = azx_dev_disconnect,
		.dev_free = azx_dev_free,
	};
	struct hda_loongson *hda;
	struct azx *chip;
	int err;

	*rchip = NULL;

	hda = kzalloc(sizeof(*hda), GFP_KERNEL);
	if (!hda) {
		dev_err(card->dev, "Cannot allocate hda\n");
		return -ENOMEM;
	}

	chip = &hda->chip;
	mutex_init(&chip->open_mutex);
	chip->card = card;
	chip->pci = pci;
	chip->ops = &loongson_hda_ops;
	chip->driver_caps = driver_caps;
	chip->driver_type = driver_caps & 0xff;
	chip->dev_index = dev;
	if (jackpoll_ms[dev] >= 50 && jackpoll_ms[dev] <= 60000)
		chip->jackpoll_interval = msecs_to_jiffies(jackpoll_ms[dev]);
	INIT_LIST_HEAD(&chip->pcm_list);
	INIT_WORK(&hda->irq_pending_work, azx_irq_pending_work);
	INIT_LIST_HEAD(&hda->list);
	init_completion(&hda->probe_wait);

	chip->get_position[0] = chip->get_position[1] = azx_get_pos_lpib;

	chip->snoop = false;
	chip->single_cmd = single_cmd;
	azx_bus(chip)->codec_mask = chip->codec_probe_mask = 0x1;


	if (bdl_pos_adj[dev] < 0) {
		switch (chip->driver_type) {
		case AZX_DRIVER_ICH:
			bdl_pos_adj[dev] = 1;
			break;
		default:
			bdl_pos_adj[dev] = 32;
			break;
		}
	}
	chip->bdl_pos_adj = bdl_pos_adj[dev];

	err = azx_bus_init(chip, model[dev]);
	if (err < 0) {
		kfree(hda);
		return err;
	}

	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
	if (err < 0) {
		dev_err(card->dev, "Error creating device [card]!\n");
		azx_free(chip);
		return err;
	}
	INIT_WORK(&hda->probe_work, azx_probe_work);

	*rchip = chip;

	return 0;
}

static int azx_first_init(struct azx *chip)
{
	int dev = chip->dev_index;
	struct pci_dev *pci = chip->pci;
	struct snd_card *card = chip->card;
	struct hdac_bus *bus = azx_bus(chip);
	int err;
	unsigned short gcap = 0;

	if (use_pci_probe) {
		err = pci_request_regions(chip->pci, "LS HD audio");
		if (err < 0)
			return err;
		chip->region_requested = 1;

		bus->addr = pci_resource_start(chip->pci, 0);
		bus->remap_addr = pci_ioremap_bar(chip->pci, 0);
	} else {
		bus->addr = platform_resource_start(to_platform_device(chip->card->dev), 0);
		bus->remap_addr = ioremap(bus->addr,
					   platform_resource_len(to_platform_device(chip->card->dev), 0));
	}
	if (bus->remap_addr == NULL) {
		dev_err(card->dev, "ioremap error\n");
		return -ENXIO;
	}

	chip->msi = 0;
	if (azx_acquire_irq(chip, 0) < 0)
		return -EBUSY;

	synchronize_irq(bus->irq);

	gcap = azx_readw(chip, GCAP);
	
	dev_dbg(card->dev, "chipset global capabilities = 0x%x\n", gcap);

	/* disable 64bit DMA address on some devices */
	if (chip->driver_caps & AZX_DCAPS_NO_64BIT) {
		dev_dbg(card->dev, "Disabling 64bit DMA\n");
		gcap &= ~AZX_GCAP_64OK;
	}

	/* disable buffer size rounding to 128-byte multiples if supported */
	if (align_buffer_size >= 0)
		chip->align_buffer_size = !!align_buffer_size;
	else {
		if (chip->driver_caps & AZX_DCAPS_NO_ALIGN_BUFSIZE)
			chip->align_buffer_size = 0;
		else
			chip->align_buffer_size = 1;
	}

	/* allow 64bit DMA address if supported by H/W */
	if ((gcap & AZX_GCAP_64OK) && !dma_set_mask(chip->card->dev, DMA_BIT_MASK(64)))
		dma_set_coherent_mask(chip->card->dev, DMA_BIT_MASK(64));
	else {
		dma_set_mask(chip->card->dev, DMA_BIT_MASK(32));
		dma_set_coherent_mask(chip->card->dev, DMA_BIT_MASK(32));
	}

	/* read number of streams from GCAP register instead of using
	 * hardcoded value
	 */
	chip->capture_streams = (gcap >> 8) & 0x0f;
	chip->playback_streams = (gcap >> 12) & 0x0f;
	if (!chip->playback_streams && !chip->capture_streams) {
		/* gcap didn't give any info, switching to old method */
		chip->playback_streams = ICH6_NUM_PLAYBACK;
		chip->capture_streams = ICH6_NUM_CAPTURE;
	}
	
	chip->capture_index_offset = 0;
	chip->playback_index_offset = chip->capture_streams;
	chip->num_streams = chip->playback_streams + chip->capture_streams;
	

	/* initialize streams */
	err = azx_init_streams(chip);
	if (err < 0)
		return err;

//	chip->playback_streams = chip->capture_streams = 1; /* Loongson */
	err = azx_alloc_stream_pages(chip);
	if (err < 0)
		return err;

	/* initialize chip */
	azx_init_chip(chip, (probe_only[dev] & 2) == 0);

	/* codec detection */
	
	if (!azx_bus(chip)->codec_mask) {
		dev_err(card->dev, "no codecs found!\n");
		return -ENODEV;
	}

	strcpy(card->driver, "HDA-Loongson");
	strlcpy(card->shortname, driver_short_names[chip->driver_type],
		sizeof(card->shortname));
	snprintf(card->longname, sizeof(card->longname),
		 "%s at 0x%lx irq %i",
		 card->shortname, bus->addr, bus->irq);

	return 0;
}


/* DMA page allocation helpers.  */
static int dma_alloc_pages(struct hdac_bus *bus, int type, size_t size,
			   struct snd_dma_buffer *buf)
{
	return snd_dma_alloc_pages(type, bus->dev, size, buf);
}

static void dma_free_pages(struct hdac_bus *bus, struct snd_dma_buffer *buf)
{
	snd_dma_free_pages(buf);
}

static int substream_alloc_pages(struct azx *chip,
				 struct snd_pcm_substream *substream,
				 size_t size)
{
	return snd_pcm_lib_malloc_pages(substream, size);
}

static int substream_free_pages(struct azx *chip,
				struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}
static int disable_msi_reset_irq(struct azx *chip)
{
	struct hdac_bus *bus = azx_bus(chip);
	int err;

	free_irq(bus->irq, chip);
	bus->irq = -1;
	chip->card->sync_irq = -1;
	pci_disable_msi(chip->pci);
	chip->msi = 0;
	err = azx_acquire_irq(chip, 1);
	if (err < 0)
		return err;

	return 0;
}

static void pcm_mmap_prepare(struct snd_pcm_substream *substream,
			     struct vm_area_struct *area)
{
	struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
	struct azx *chip = apcm->chip;
	if (chip->uc_buffer)
		area->vm_page_prot = pgprot_writecombine(area->vm_page_prot);
}
static const struct hda_controller_ops loongson_hda_ops = {
	.disable_msi_reset_irq = disable_msi_reset_irq,
	.position_check = azx_position_check,
};

static int azx_probe(struct platform_device *pdev)
{
	static int dev;
	struct snd_card *card;
	struct hda_loongson *hda;
	struct azx *chip;
	bool probe_now;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}
	err = snd_card_new(&pdev->dev, index[dev], id[dev], THIS_MODULE,
			   0, &card);
	if (err < 0) {
		dev_err(&pdev->dev, "Error creating card!\n");
		return err;
	}

	err = azx_create(card, NULL, dev, AZX_DRIVER_ICH | AZX_DCAPS_LS_HDA_WORKAROUND, &chip);
	if (err < 0)
		goto out_free;
	card->private_data = chip;
	hda = container_of(chip, struct hda_loongson, chip);

	dev_set_drvdata(&pdev->dev, card);

	probe_now = !chip->disabled;

	if (probe_now)
		schedule_work(&hda->probe_work);
		
	dev++;
	if (chip->disabled)
		complete_all(&hda->probe_wait);
	return 0;

out_free:
	snd_card_free(card);
	return err;
}

/* number of codec slots for each chipset: 0 = default slots (i.e. 4) */
static unsigned int azx_max_codecs[AZX_NUM_DRIVERS] = {};

static int azx_probe_continue(struct azx *chip)
{
	struct hda_loongson *hda = container_of(chip, struct hda_loongson, chip);
	struct hdac_bus *bus = azx_bus(chip);
	int dev = chip->dev_index;
	int err;
	struct device *snddev = chip->card->dev;

	to_hda_bus(bus)->bus_probing = 1;
	hda->probe_continued = 1;
	err = azx_first_init(chip);
	if (err < 0)
		goto out_free;

#ifdef CONFIG_SND_HDA_INPUT_BEEP
	chip->beep_mode = beep_mode[dev];
#endif

	/* create codec instances */
	err = azx_probe_codecs(chip, azx_max_codecs[chip->driver_type]);
	if (err < 0)
		goto out_free;
	if ((probe_only[dev] & 1) == 0) {
		err = azx_codec_configure(chip);
		if (err < 0)
			goto out_free;
	}
	err = snd_card_register(chip->card);
	if (err < 0)
		goto out_free;

	chip->running = 1;
	azx_add_card_list(chip);
#ifdef CONFIG_PM
	pm_runtime_forbid(snddev);
	pm_runtime_set_active(snddev);
#endif
	snd_hda_set_power_save(&chip->bus, power_save * 1000);
	if (azx_has_pm_runtime(chip))
		pm_runtime_put_noidle(snddev);

out_free:
	complete_all(&hda->probe_wait);
	to_hda_bus(bus)->bus_probing = 0;
	return err;
}

static int azx_remove(struct platform_device *pdev)
{
	return snd_card_free(dev_get_drvdata(&pdev->dev));
}

static void azx_shutdown(struct platform_device *pdev)
{
	struct snd_card *card = dev_get_drvdata(&pdev->dev);
	struct azx *chip;

	if (!card)
		return;
	chip = card->private_data;
	if (chip && chip->running)
		azx_stop_chip(chip);
}

#ifdef CONFIG_OF
static struct of_device_id ls_hda_id_table[] = {
	{.compatible = "loongson,ls-audio"},
	{},
};
MODULE_DEVICE_TABLE(of, ls_hda_id_table);
#endif

/* platform_driver definition */
static struct platform_driver azx_driver = {
	.probe = azx_probe,
	.remove = azx_remove,
	.shutdown = azx_shutdown,
	.driver = {
		.name = "ls-audio",
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(ls_hda_id_table),
#endif
		.pm = AZX_PM_OPS,
	},
};

#define PCI_DEVICE_ID_HDA 0x7a07
#define PCI_VENDOR_ID_HDA 0x0014
/*
static DEFINE_PCI_DEVICE_TABLE(azx_id_table) = {
	{PCI_DEVICE(PCI_VENDOR_ID_HDA, PCI_DEVICE_ID_HDA)},
	{}
};
MODULE_DEVICE_TABLE(pci, azx_id_table);
*/
static const struct pci_device_id azx_id_table[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_HDA, PCI_DEVICE_ID_HDA)},
	{}
};
MODULE_DEVICE_TABLE(pci, azx_id_table);

static int azx_pci_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct hda_loongson *hda;
	struct azx *chip;
	bool schedule_probe;
	int err;
	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	use_pci_probe = 1;

	err = pci_enable_device(pci);
	if (err < 0)
		return err;

	err = snd_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE, 0, &card);
	if (err < 0) {
		dev_err(&pci->dev, "Error creating card!\n");
		return err;
	}
	err = azx_create(card, pci, dev, AZX_DRIVER_ICH | AZX_DCAPS_LS_HDA_WORKAROUND, &chip);
	if (err < 0)
		goto out_free;
	card->private_data = chip;
	hda = container_of(chip, struct hda_loongson, chip);

	pci_set_drvdata(pci, card);

	schedule_probe = !chip->disabled;

	if (schedule_probe)
		schedule_work(&hda->probe_work);
	dev++;
	if (chip->disabled)
		complete_all(&hda->probe_wait);
	return 0;

out_free:
	snd_card_free(card);
	return err;
}

static void azx_pci_remove(struct pci_dev *pci)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct azx *chip;
	struct hda_loongson *hda;

	if (card) {
		/* cancel the pending probing work */
		chip = card->private_data;
		hda = container_of(chip, struct hda_loongson, chip);
		/* FIXME: below is an ugly workaround.
		 * Both device_release_driver() and driver_probe_device()
		 * take *both* the device's and its parent's lock before
		 * calling the remove() and probe() callbacks.  The codec
		 * probe takes the locks of both the codec itself and its
		 * parent, i.e. the PCI controller dev.  Meanwhile, when
		 * the PCI controller is unbound, it takes its lock, too
		 * ==> ouch, a deadlock!
		 * As a workaround, we unlock temporarily here the controller
		 * device during cancel_work_sync() call.
		 */
		device_unlock(&pci->dev);
		cancel_work_sync(&hda->probe_work);
		device_lock(&pci->dev);

		snd_card_free(card);
	}

}

static void azx_pci_shutdown(struct pci_dev *pci)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct azx *chip;

	if (!card)
		return;
	chip = card->private_data;
	if (chip && chip->running)
		azx_stop_chip(chip);
}

/* pci_driver definition */
static struct pci_driver azx_pci_driver = {
	.name = "azx_pci_driver",
	.id_table = azx_id_table,
	.probe = azx_pci_probe,
	.remove = azx_pci_remove,
	.shutdown = azx_pci_shutdown,
	.driver = {
		.pm = AZX_PM_OPS,
	},
};

static int __init alsa_card_azx_init(void)
{
	int err;
	err = platform_driver_register(&azx_driver);
//	if (!err) {
//		err = pci_register_driver(&azx_pci_driver);
//	}

	printk("hda azx driver register err:%d\n", err); 
	return err;
}

static void __exit alsa_card_azx_exit(void)
{
	platform_driver_unregister(&azx_driver);
}

module_init(alsa_card_azx_init)
module_exit(alsa_card_azx_exit)
