/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2019 Jiaxun Yang <jiaxun.yang@flygoat.com>
 *
 * Built-in Generic dtbs for MACH_LOONGSON64
 */

#ifndef __ASM_MACH_LOONGSON64_BUILTIN_DTBS_H_
#define __ASM_MACH_LOONGSON64_BUILTIN_DTBS_H_

extern u32 __dtb_loongson2k_begin[];
extern u32 __dtb_loongson3_4core_rs780e_begin[];
extern u32 __dtb_loongson3_8core_rs780e_begin[];
extern u32 __dtb_loongson3_4core_ls7a_begin[];
extern u32 __dtb_loongson3_r4_ls7a_begin[];

extern void __init *get_builtin_dtb(void);

#endif
