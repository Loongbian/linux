/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999, 2000, 03, 04 Ralf Baechle
 * Copyright (C) 2000, 2002  Maciej W. Rozycki
 * Copyright (C) 1990, 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_MACH_GENERIC_SPACES_H
#define _ASM_MACH_GENERIC_SPACES_H

#include <asm/addrspace.h>
#include <asm/mipsregs.h>
#include <asm/page-def.h>

/*
 * This gives the physical RAM offset.
 */
#ifndef __ASSEMBLY__
# if defined(CONFIG_MIPS_AUTO_PFN_OFFSET)
#  define PHYS_OFFSET		((unsigned long)PFN_PHYS(ARCH_PFN_OFFSET))
# elif !defined(PHYS_OFFSET)
#  define PHYS_OFFSET		_AC(0, UL)
# endif
#endif /* __ASSEMBLY__ */

#ifdef CONFIG_32BIT
#ifdef CONFIG_KVM_GUEST
#define CAC_BASE		_AC(0x40000000, UL)
#else
#define CAC_BASE	CKSEG0
#endif
#ifndef IO_BASE
#define IO_BASE		CKSEG1
#endif
#ifndef UNCAC_BASE
#define UNCAC_BASE	CKSEG1
#endif

#ifndef MAP_BASE
#ifdef CONFIG_KVM_GUEST
#define MAP_BASE		_AC(0x60000000, UL)
#else
#define MAP_BASE		CKSEG2
#endif
#endif

/*
 * Memory above this physical address will be considered highmem.
 */
#ifndef HIGHMEM_START
#define HIGHMEM_START		_AC(0x20000000, UL)
#endif

#endif /* CONFIG_32BIT */

#ifdef CONFIG_64BIT

#ifndef CAC_BASE
#define CAC_BASE		PHYS_TO_XKPHYS(read_c0_config() & CONF_CM_CMASK, 0)
#endif

#ifndef IO_BASE
#define IO_BASE			PHYS_TO_XKPHYS(K_CALG_UNCACHED, 0)
#endif

#ifndef UNCAC_BASE
#define UNCAC_BASE		PHYS_TO_XKPHYS(K_CALG_UNCACHED, 0)
#endif

#ifndef MAP_BASE
#define MAP_BASE		XKSEG
#endif

/*
 * Memory above this physical address will be considered highmem.
 * Fixme: 59 bits is a fictive number and makes assumptions about processors
 * in the distant future.  Nobody will care for a few years :-)
 */
#ifndef HIGHMEM_START
#define HIGHMEM_START		(_AC(1, UL) << _AC(59, UL))
#endif

#define TO_PHYS(x)		(	      ((x) & TO_PHYS_MASK))
#define TO_CAC(x)		(CAC_BASE   | ((x) & TO_PHYS_MASK))
#define TO_UNCAC(x)		(UNCAC_BASE | ((x) & TO_PHYS_MASK))

#endif /* CONFIG_64BIT */

/*
 * This handles the memory map.
 */
#ifndef PAGE_OFFSET
#define PAGE_OFFSET		(CAC_BASE + PHYS_OFFSET)
#endif

#ifndef FIXADDR_TOP
#ifdef CONFIG_KVM_GUEST
#define FIXADDR_TOP		((unsigned long)(long)(int)0x7ffe0000)
#else
#define FIXADDR_TOP		(CKSEG3 + 0x1ffe0000)
#endif
#endif

#ifdef CONFIG_PCI_IO_VMMAP
#define PCI_IO_SIZE		SZ_16M
#else
#define PCI_IO_SIZE		0
#endif

#ifdef CONFIG_64BIT
/*
 * TLB refill handlers also map the vmalloc area into xuseg.  Avoid
 * the first couple of pages so NULL pointer dereferences will still
 * reliably trap.
 */
#define PCI_IO_START		(MAP_BASE + (2 * PAGE_SIZE))
#else
#define PCI_IO_START		MAP_BASE
#endif

#define PCI_IO_END		(PCI_IO_START + PCI_IO_SIZE)
#define VMALLOC_START		PCI_IO_END

#endif /* __ASM_MACH_GENERIC_SPACES_H */
