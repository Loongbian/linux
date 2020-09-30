#include <linux/io.h>
#include <loongson.h>
#include "loongson_drv.h"

#define PLL_REF_CLK_MHZ    100
#define PCLK_PRECISION_INDICATOR 10000

static void ls2k_config_pll(void *pll_base, struct pll_config *pll_cfg)
{
	unsigned long val;

	/* set sel_pll_out0 0 */
	val = readq(pll_base);
	val &= ~BIT(0);
	writeq(val, pll_base);

	/* pll_pd 1 */
	val = readq(pll_base);
	val |= BIT(19);
	writeq(val, pll_base);

	/* set_pll_param 0 */
	val = readq(pll_base);
	val &= ~BIT(2);
	writeq(val, pll_base);

	/* set new div ref, loopc, div out */
	/* clear old value first*/
	val = BIT(7) | (BIT(10) | BIT(11)) | BIT(42) |
		((u64)(pll_cfg->l1_loopc) << 32) |
		((u64)(pll_cfg->l1_frefc) << 26);
	writeq(val, pll_base);
	writeq(pll_cfg->l2_div0, pll_base + 8);

	/* set_pll_param 1 */
	val = readq(pll_base);
	val |= BIT(2);
	writeq(val, pll_base);

	/* pll_pd 0 */
	val = readq(pll_base);
	val &= ~BIT(19);
	writeq(val, pll_base);

	/* wait pll lock */
	while(!(readl(pll_base) & BIT(16)));
	/* set sel_pll_out0 1 */
	val = readq(pll_base);
	val |= BIT(0);
	writeq(val, pll_base);
}


/**
 * ls7a_cal_freq
 *
 * @pixclock: unsigned int
 * @pll_config: point to the pll_config structure
 *
 * Calculate frequency
 */
static unsigned int ls7a_cal_freq(unsigned int pixclock, struct pll_config *pll_config)
{
	int i, j, loopc_offset;
	unsigned int refc_set[] = {4, 5, 3};
	unsigned int prec_set[] = {1, 5, 10, 50, 100};   /*in 1/PCLK_PRECISION_INDICATOR*/
	unsigned int pstdiv, loopc, refc;
	unsigned int precision_req, precision;
	unsigned int loopc_min, loopc_max, loopc_mid;
	unsigned long long real_dvo, req_dvo;

	/*try precision from high to low*/
	for (j = 0; j < sizeof(prec_set)/sizeof(int); j++){
		precision_req = prec_set[j];

		/*try each refc*/
		for (i = 0; i < sizeof(refc_set)/sizeof(int); i++) {
			refc = refc_set[i];
			loopc_min = (1200 / PLL_REF_CLK_MHZ) * refc;  /*1200 / (PLL_REF_CLK_MHZ / refc)*/
			loopc_max = (3200 / PLL_REF_CLK_MHZ) * refc;  /*3200 / (PLL_REF_CLK_MHZ / refc)*/
			loopc_mid = (2200 / PLL_REF_CLK_MHZ) * refc;  /*(loopc_min + loopc_max) / 2;*/
			loopc_offset = -1;

			/*try each loopc*/
			for (loopc = loopc_mid; (loopc <= loopc_max) && (loopc >= loopc_min); loopc += loopc_offset) {
				if(loopc_offset < 0)
					loopc_offset = -loopc_offset;
				else
					loopc_offset = -(loopc_offset+1);

				pstdiv = loopc * PLL_REF_CLK_MHZ * 1000 / refc / pixclock;
				if((pstdiv > 127) || (pstdiv < 1))
					continue;

				/*real_freq is float type which is not available, but read_freq * pstdiv is available.*/
				req_dvo  = (pixclock * pstdiv);
				real_dvo = (loopc * PLL_REF_CLK_MHZ * 1000 / refc);
				precision = abs(real_dvo * PCLK_PRECISION_INDICATOR / req_dvo - PCLK_PRECISION_INDICATOR);

				if(precision < precision_req){
					pll_config->l2_div0 = pstdiv;
					pll_config->l2_div1 = 0x0;
					pll_config->l2_div2 = 0x0;
					pll_config->l1_loopc = loopc;
					pll_config->l1_frefc = refc;
					if(j > 1)
						printk("Warning: PIX clock precision degraded to %d / %d\n", precision_req, PCLK_PRECISION_INDICATOR);
					return 1;
				}
			}
		}
	}
	return 0;
}



void ls7a_read_cur_pll(void __iomem *pll_base,struct pll_config *pll_config)
{
	uint32_t val;
	uint32_t out0, out1, out2;
	val = readl(pll_base);
	pr_info("pll_val0: %lx", val);
	pll_config->l1_frefc = val & GENMASK(6,0);
	pr_info("pll_config->l1_frefc: %d", pll_config->l1_frefc);
	val = readl(pll_base + 4);
	pr_info("pll_val1: %lx", val);

	pll_config->l2_div0 = val & GENMASK(6,0);
	pll_config->l2_div1 = (val & GENMASK(13,7)) >> 7;
	pll_config->l2_div2 = (val & GENMASK(20,14)) >> 14;
	pll_config->l1_loopc = (val & GENMASK(29,21)) >> 21;
	pr_info("pll_config->l1_div0: %d", pll_config->l2_div0);
	pr_info("pll_config->l1_div1: %d", pll_config->l2_div1);
	pr_info("pll_config->l1_div2: %d", pll_config->l2_div2);
	pr_info("pll_config->l1_loopc: %d", pll_config->l1_loopc);


	out0 = 100000000 / pll_config->l1_frefc * pll_config->l1_loopc / pll_config->l2_div0;
	out1 = 100000000 / pll_config->l1_frefc * pll_config->l1_loopc / pll_config->l2_div1;
	out2 = 100000000 / pll_config->l1_frefc * pll_config->l1_loopc / pll_config->l2_div2;

	pr_info("cur_pll: out0: %d, out1: %d, out2: %d", out0, out1, out2);
}

/**
 * ls7a_config_pll
 *
 * @pll_base: represent a long type
 * @pll_cfg: point to the pll_config srtucture
 *
 * Config pll apply to ls7a
 */
void ls7a_config_pll(void *pll_base, struct pll_config *pll_cfg)
{
	unsigned long val;

	/* clear sel_pll_out0 */
	val = readl(pll_base + 0x4);
	val &= ~(1UL << 8);
	writel(val, pll_base + 0x4);
	/* set pll_pd */
	val = readl(pll_base + 0x4);
	val |= (1UL << 13);
	writel(val, pll_base + 0x4);
	/* clear set_pll_param */
	val = readl(pll_base + 0x4);
	val &= ~(1UL << 11);
	writel(val, pll_base + 0x4);
	/* clear old value & config new value */
	val = readl(pll_base + 0x4);
	val &= ~(0x7fUL << 0);
	val |= (pll_cfg->l1_frefc << 0); /* refc */
	writel(val, pll_base + 0x4);
	val = readl(pll_base + 0x0);
	val &= ~(GENMASK(20, 0) << 0);
	if(pll_cfg->l2_div0)
		val |= (pll_cfg->l2_div0 << 0);   /* div 0*/
	if(pll_cfg->l2_div1)
		val |= (pll_cfg->l2_div1 << 7);   /* div 1*/
	if(pll_cfg->l2_div2)
		val |= (pll_cfg->l2_div2 << 14);   /* div 2*/
	val &= ~(0x1ffUL << 21);
	val |= (pll_cfg->l1_loopc << 21);/* loopc */
	writel(val, pll_base + 0x0);
	/* set set_pll_param */
	val = readl(pll_base + 0x4);
	val |= (1UL << 11);
	writel(val, pll_base + 0x4);
	/* clear pll_pd */
	val = readl(pll_base + 0x4);
	val &= ~(1UL << 13);
	writel(val, pll_base + 0x4);
	/* wait pll lock */
	while(!(readl(pll_base + 0x4) & 0x80))
		cpu_relax();
	/* set sel_pll_out0 1 2*/
	val = readl(pll_base + 0x4);
	if(pll_cfg->l2_div0)
		val |= (1UL << 8);
	if(pll_cfg->l2_div1)
		val |= (1UL << 9);
	if(pll_cfg->l2_div2)
		val |= (1UL << 10);
	writel(val, pll_base + 0x4);
}

void ls_config_pll(int id, unsigned int pix_freq)
{
	unsigned int out;
	struct pll_config pll_cfg;

	if ((read_c0_prid() & PRID_IMP_MASK) == PRID_IMP_LOONGSON_64R) {
		out = ls7a_cal_freq(pix_freq, &pll_cfg);
		if (id == 0)
			ls2k_config_pll(TO_UNCAC(0x1fe104b0), &pll_cfg);
		else
			ls2k_config_pll(TO_UNCAC(0x1fe104c0), &pll_cfg);
	} else {
		out = ls7a_cal_freq(pix_freq, &pll_cfg);
		if (id == 0)
			ls7a_config_pll(LS7A_PIX0_PLL, &pll_cfg);
		else
			ls7a_config_pll(LS7A_PIX1_PLL, &pll_cfg);
	}
}