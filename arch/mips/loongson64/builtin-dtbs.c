// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Jiaxun Yang <jiaxun.yang@flygoat.com>
 *
 * Load and patch built-in Generic dtbs
 */

#include <linux/init.h>
#include <linux/libfdt.h>
#include <linux/pci_ids.h>

#include <asm/bootinfo.h>
#include <builtin_dtbs.h>
#include <loongson.h>


#define LS3_PCI_CONF_BASE ((void __iomem *)TO_UNCAC(0x1a000000))

enum cpu_types {
    CPU_NONE,
    CPU_LS2K,
    CPU_LS3_4CORES,
    CPU_LS3_8CORES,
    CPU_LS3_R4,
};

enum pch_types {
    PCH_NONE,
    PCH_SOC,
    PCH_LS7A,
    PCH_RS780E,
};

static void __init patch_pci_ranges(void *fdt)
{
    int off, len;
    u64 address, size;
    __be32 *ranges;

    address = loongson_sysconf.pci_mem_start_addr;
    size = loongson_sysconf.pci_mem_end_addr - \
        loongson_sysconf.pci_mem_start_addr + 1;

    if (!address)
        return;

    pr_info("Builtin-dtbs: Patching PCI ranges\n");
    pr_info("PCI MEM start: 0x%llx size: 0x%llx\n", address, size);

    off = fdt_path_offset(fdt, "/bus@10000000/pci@1a000000");
    if (off < 0) {
        pr_err("Builtin-dtbs: Can't find PCI Controller Node\n");
        return;
    }

    ranges = fdt_getprop_w(fdt, off, "ranges", &len);
    if (!ranges || len != (7 * 2 * sizeof(__be32))) {
        pr_err("Builtin-dtbs: ranges prop invalid\n");
        return;
    }

    /* Second range is MEM */
    /* CPU ADDR */
    ranges[7 + 1] = cpu_to_be32(address >> 32);
    ranges[7 + 2] = cpu_to_be32(address & 0xffffffff);
    /* BUS ADDR, same as CPU ADDR */
    ranges[7 + 3] = ranges[7 + 1];
    ranges[7 + 4] = ranges[7 + 2];
    /* SIZE */
    ranges[7 + 5] = cpu_to_be32(size >> 32);
    ranges[7 + 6] = cpu_to_be32(size & 0xffffffff);
}

/* Read the ID of PCI host bridge to detect bridge type */
static int __init get_pch_type_by_pci(void)
{
    u32 id;
    u16 vendor, device;

    /* Host Bridge: Bus 0 Device 0 Function 0  */
    id = readl(LS3_PCI_CONF_BASE);
    vendor = id & 0xffff;
    device = (id >> 16) & 0xffff;

    if (vendor == PCI_VENDOR_ID_LOONGSON && device == 0x7a00)
        return PCH_LS7A;

    if (vendor == PCI_VENDOR_ID_AMD)
        return PCH_RS780E;

    return PCH_NONE;
}

void __init *get_builtin_dtb(void)
{
    void *fdt;
    int cpu = CPU_NONE;
    int pch = PCH_NONE;

    if ((read_c0_prid() & PRID_IMP_MASK) == PRID_IMP_LOONGSON_64C) {
		switch (read_c0_prid() & PRID_REV_MASK) {
		case PRID_REV_LOONGSON3A_R1:
		case PRID_REV_LOONGSON3A_R2_0:
		case PRID_REV_LOONGSON3A_R2_1:
		case PRID_REV_LOONGSON3A_R3_0:
		case PRID_REV_LOONGSON3A_R3_1:
			cpu = CPU_LS3_4CORES;
			break;
		case PRID_REV_LOONGSON3B_R1:
		case PRID_REV_LOONGSON3B_R2:
			cpu = CPU_LS3_8CORES;
			break;
		default:
			break;
		}
    }

    if ((read_c0_prid() & PRID_IMP_MASK) == PRID_IMP_LOONGSON_64G)
			cpu = CPU_LS3_R4;

    if ((read_c0_prid() & PRID_IMP_MASK) == PRID_IMP_LOONGSON_64R)
			cpu = CPU_LS2K;

    if (cpu == CPU_NONE) {
        pr_err("Builtin-dtbs: Failed to determine CPU Type\n");
        return NULL;
    }

    switch(cpu) {
    case CPU_LS3_4CORES:
    case CPU_LS3_8CORES:
    case CPU_LS3_R4:
        pch = get_pch_type_by_pci();
        break;
    case CPU_LS2K:
        pch = PCH_SOC;
        break;
    default:
        break;
    }

    if (pch == PCH_NONE) {
        pr_err("Builtin-dtbs: Failed to determine CPU Type\n");
        return NULL;
    }

    if (cpu == CPU_LS2K && pch == PCH_SOC)
        fdt = __dtb_loongson2k_begin;

    if (cpu == CPU_LS3_4CORES && pch == PCH_RS780E)
        fdt = __dtb_loongson3_4core_rs780e_begin;

    if (cpu == CPU_LS3_8CORES && pch == PCH_RS780E)
        fdt = __dtb_loongson3_8core_rs780e_begin;
    
    if (cpu == CPU_LS3_4CORES && pch == PCH_LS7A)
        fdt = __dtb_loongson3_4core_ls7a_begin;

    if (cpu == CPU_LS3_R4 && pch == PCH_LS7A)
        fdt = __dtb_loongson3_r4_ls7a_begin;

    if (!fdt) {
        pr_err("Builtin-dtbs: No matching model\n");
        return NULL;
    }

    patch_pci_ranges(fdt);

    return fdt;
}