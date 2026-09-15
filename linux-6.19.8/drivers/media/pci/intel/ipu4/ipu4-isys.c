// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Intel Corporation

#include <linux/module.h>

#include "ipu.h"
#include "ipu-platform-regs.h"
#include "ipu-platform-buttress-regs.h"
#include "ipu-platform-isys-csi2-reg.h"
#include "ipu-trace.h"
#include "ipu-isys.h"
#include "ipu-isys-video.h"
#include "ipu-isys-tpg.h"

struct ipu_trace_block isys_trace_blocks[] = {
	{
	 .offset = TRACE_REG_IS_TRACE_UNIT_BASE,
	 .type = IPU_TRACE_BLOCK_TUN,
	},
	{
	 .offset = TRACE_REG_IS_SP_EVQ_BASE,
	 .type = IPU_TRACE_BLOCK_TM,
	},
	{
	 .offset = TRACE_REG_IS_SP_GPC_BASE,
	 .type = IPU_TRACE_BLOCK_GPC,
	},
	{
	 .offset = TRACE_REG_IS_ISL_GPC_BASE,
	 .type = IPU_TRACE_BLOCK_GPC,
	},
	{
	 .offset = TRACE_REG_IS_MMU_GPC_BASE,
	 .type = IPU_TRACE_BLOCK_GPC,
	},
	{
	 .offset = TRACE_REG_CSI2_TM_BASE,
	 .type = IPU_TRACE_CSI2,
	},
	{
	 .offset = TRACE_REG_CSI2_3PH_TM_BASE,
	 .type = IPU_TRACE_CSI2_3PH,
	},
	{
	 /* Note! this covers all 9 blocks */
	 .offset = TRACE_REG_CSI2_SIG2SIO_GR_BASE(0),
	 .type = IPU_TRACE_SIG2CIOS,
	},
	{
	 /* Note! this covers all 9 blocks */
	 .offset = TRACE_REG_CSI2_PH3_SIG2SIO_GR_BASE(0),
	 .type = IPU_TRACE_SIG2CIOS,
	},
	{
	 .offset = TRACE_REG_IS_GPREG_TRACE_TIMER_RST_N,
	 .type = IPU_TRACE_TIMER_RST,
	},
	{
	 .type = IPU_TRACE_BLOCK_END,
	}
};

#ifdef CONFIG_VIDEO_INTEL_IPU4
void isys_setup_hw(struct ipu_isys *isys)
{
	void __iomem *base = isys->pdata->base;
	const u8 *thd = isys->pdata->ipdata->hw_variant.cdc_fifo_threshold;
	u32 irqs;
	unsigned int i;

	/* Enable irqs for all MIPI busses */
	irqs = IPU_ISYS_UNISPART_IRQ_CSI2(0) |
	    IPU_ISYS_UNISPART_IRQ_CSI2(1) |
	    IPU_ISYS_UNISPART_IRQ_CSI2(2) |
	    IPU_ISYS_UNISPART_IRQ_CSI2(3) |
	    IPU_ISYS_UNISPART_IRQ_CSI2(4) | IPU_ISYS_UNISPART_IRQ_CSI2(5);

	irqs |= IPU_ISYS_UNISPART_IRQ_SW;

	writel(irqs, base + IPU_REG_ISYS_UNISPART_IRQ_EDGE);
	writel(irqs, base + IPU_REG_ISYS_UNISPART_IRQ_LEVEL_NOT_PULSE);
	writel(irqs, base + IPU_REG_ISYS_UNISPART_IRQ_CLEAR);
	writel(irqs, base + IPU_REG_ISYS_UNISPART_IRQ_MASK);
	writel(irqs, base + IPU_REG_ISYS_UNISPART_IRQ_ENABLE);

	writel(0, base + IPU_REG_ISYS_UNISPART_SW_IRQ_REG);
	writel(0, base + IPU_REG_ISYS_UNISPART_SW_IRQ_MUX_REG);

	/* Write CDC FIFO threshold values for isys */
	for (i = 0; i < isys->pdata->ipdata->hw_variant.cdc_fifos; i++)
		writel(thd[i], base + IPU_REG_ISYS_CDC_THRESHOLD(i));
}
#endif

#ifdef CONFIG_VIDEO_INTEL_IPU4P
/*
 * For new HW, extra common register (en_flush_for_idrain)added to the IBufCtrl
 * of ISL_IS and CSI that enables the feature to send a DMA command with flush
 * when draining. This means that a DMA command is send with the flush bit
 * set(read post write check is performed) when a drain request comes in and
 * iwake is enabled for that SID proc.
 * This results in that all data is moved out of the system when the IDone is
 * given back. Default the feature is off, to keep behavior as is when nothing
 * is written, writing 0x1 to the register (reg 11 in common reg bank,
 * addr ibuf_base + 0x2C) to enable this feature.
 */
static int ipu4p_isys_flush_idrain_en(struct ipu_isys *isys)
{
	void __iomem *base = isys->pdata->base;

	writel(1, base + CSI2_REG_CL0_IBUFCTL_EN_FLUSH_FOR_IDRAIN);
	writel(1, base + CSI2_REG_CL1_IBUFCTL_EN_FLUSH_FOR_IDRAIN);
	writel(1, base + IPU_REG_ISYS_IBUFCTL_EN_FLUSH_FOR_IDRAIN);

	return 0;
}

static void ipu4p_isys_irq_cfg(struct ipu_isys *isys)
{
	void __iomem *base = isys->pdata->base;
	int i, j;
	struct {
		u32 base;
		u32 mask;
	} irq_config[] = {
		{IPU_REG_ISYS_UNISPART_IRQ_EDGE, 0x400018},
		{IPU_REG_ISYS_ISA_ACC_IRQ_CTRL_BASE, 0x0},
		{IPU_REG_ISYS_A_IRQ_CTRL_BASE, 0x0},
		{IPU_REG_ISYS_SIP0_IRQ_CTRL_BASE, 0xf},
		{IPU_REG_ISYS_SIP1_IRQ_CTRL_BASE, 0xf},
	};
	unsigned int offsets[4] = {
		0x0, 0x4, 0x10, 0x14
	};

	for (i = 0; i < ARRAY_SIZE(irq_config); i++) {
		for (j = 0; j < ARRAY_SIZE(offsets); j++)
			writel(irq_config[i].mask,
				   base + irq_config[i].base + offsets[j]);
		writel(0xffffffff, base + irq_config[i].base + 0xc);
	}

	writel(0, base + IPU_REG_ISYS_UNISPART_SW_IRQ_REG);
	writel(0, base + IPU_REG_ISYS_UNISPART_SW_IRQ_MUX_REG);
}

/*
 * The builtin bbconfig below configures the PHY building blocks for
 * bbs 4/6 and 12/14 only. On Surface Pro 7 the front ov5693 (sip1
 * port 1, x2) appears to sit on bbs that are not in the list, so its
 * analog front end never gets configured and the receiver sees no HS
 * traffic. Allow extra bbs to be configured at runtime; consulted on
 * every isys resume (i.e. after each power island cycle), e.g.:
 *   echo 8,10 > /sys/module/intel_ipu4p_isys/parameters/phy_bb_extra
 * then cycle the island and retry the capture.
 */
static int phy_bb_extra[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
module_param_array(phy_bb_extra, int, NULL, 0644);
MODULE_PARM_DESC(phy_bb_extra,
		 "Extra PHY building blocks to configure (-1 = none)");

static int phy_afe_extra[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
module_param_array(phy_afe_extra, int, NULL, 0644);
MODULE_PARM_DESC(phy_afe_extra,
		 "AFE config value per extra bb (-1 = alternate 0xf/0x15)");

static bool phy_jsl_bits;
module_param(phy_jsl_bits, bool, 0644);
MODULE_PARM_DESC(phy_jsl_bits,
		 "Also set JSL-style CPHY_RX_CONTROL1/DPHY_CFG bits on all bbs");

static void ipu4p_isys_bb_cfg_one(struct ipu_isys *isys, unsigned int bb,
				  unsigned int crc, unsigned int drc,
				  unsigned int afe)
{
	void __iomem *isp_base = isys->adev->isp->base;
	u32 val, cphy, dphy, afe_readback;

	val = readl(isp_base + BUTTRESS_REG_CPHYX_DLL_OVRD(bb));
	val &= ~0x7e;
	val |= crc << 1;
	val |= 1;
	writel(val, isp_base + BUTTRESS_REG_CPHYX_DLL_OVRD(bb));
	val = readl(isp_base + BUTTRESS_REG_DPHYX_DLL_OVRD(bb));
	val |= 1;
	val |= drc << 1;
	writel(val, isp_base + BUTTRESS_REG_DPHYX_DLL_OVRD(bb));
	val = afe | (2 << 29);
	writel(val, isp_base + BUTTRESS_REG_BBX_AFE_CONFIG(bb));

	cphy = readl(isp_base + BUTTRESS_REG_CPHYX_DLL_OVRD(bb));
	dphy = readl(isp_base + BUTTRESS_REG_DPHYX_DLL_OVRD(bb));
	afe_readback = readl(isp_base + BUTTRESS_REG_BBX_AFE_CONFIG(bb));
	dev_info(&isys->adev->dev,
		 "trace phy bb%u: requested crc=%u drc=%u afe=0x%x "
		 "readback cphy_dll=0x%x dphy_dll=0x%x afe=0x%x\n",
		 bb, crc, drc, afe, cphy, dphy, afe_readback);
}

static void ipu4p_isys_bb_cfg(struct ipu_isys *isys)
{
	void __iomem *isp_base = isys->adev->isp->base;
	unsigned int i, val;
	unsigned int bbconfig[5][4] = {
		{4, 13, 32, 0xf},
		{6, 13, 32, 0x15},
		{12, 13, 32, 0xf},
		{14, 13, 32, 0x15},
		/*
		 * Surface Pro 7: the front ov5693 (sip1, x2) sits on
		 * building block 10, which the original list left
		 * unconfigured — its AFE never powered up and the
		 * receiver saw no HS traffic. AFE 0x15 matches the JSL
		 * x2 value; verified stable (0 sync errors). Do NOT
		 * configure bb 8 the same way: it injects DPHY sync
		 * errors on bb 10 (bb 8 is likely the ov7251 IR lane).
		 */
		{10, 13, 32, 0x15},
	};

	/* Config building block */
	for (i = 0; i < ARRAY_SIZE(bbconfig); i++)
		ipu4p_isys_bb_cfg_one(isys, bbconfig[i][0], bbconfig[i][1],
				      bbconfig[i][2], bbconfig[i][3]);

	for (i = 0; i < ARRAY_SIZE(phy_bb_extra); i++) {
		int bb = phy_bb_extra[i];
		unsigned int afe;

		if (bb < 0)
			continue;
		afe = phy_afe_extra[i] >= 0 ? phy_afe_extra[i] :
			((i & 1) ? 0x15 : 0xf);
		dev_info(&isys->adev->dev,
			 "phy: extra bb %d cfg, afe 0x%x\n", bb, afe);
		ipu4p_isys_bb_cfg_one(isys, bb, 13, 32, afe);
	}

	if (phy_jsl_bits) {
		dev_info(&isys->adev->dev, "phy: applying JSL-style bits\n");
		for (i = 0; i < 16; i += 2) {
			val = readl(isp_base +
				    BUTTRESS_REG_CPHYX_RX_CONTROL1(i));
			val |= BIT(31);
			writel(val, isp_base +
			       BUTTRESS_REG_CPHYX_RX_CONTROL1(i));
			/*
			 * DPHY_CFG is 4 bytes below DPHY_DLL_OVRD (0x148 vs
			 * 0x14c); no dedicated macro exists in the header.
			 */
			val = readl(isp_base +
				    BUTTRESS_REG_DPHYX_DLL_OVRD(i) - 4);
			val |= BIT(25) | BIT(26);
			writel(val, isp_base +
			       BUTTRESS_REG_DPHYX_DLL_OVRD(i) - 4);
		}
	}

	/* Log full PHY bb state to aid bring-up diagnosis */
	for (i = 0; i < 16; i += 2) {
		dev_dbg(&isys->adev->dev,
			"phy bb %u: cphy_dll=0x%x rx_ctrl1=0x%x dphy_cfg=0x%x dphy_dll=0x%x afe=0x%x\n",
			i,
			readl(isp_base + BUTTRESS_REG_CPHYX_DLL_OVRD(i)),
			readl(isp_base + BUTTRESS_REG_CPHYX_RX_CONTROL1(i)),
			readl(isp_base + BUTTRESS_REG_DPHYX_DLL_OVRD(i) - 4),
			readl(isp_base + BUTTRESS_REG_DPHYX_DLL_OVRD(i)),
			readl(isp_base + BUTTRESS_REG_BBX_AFE_CONFIG(i)));
	}
}

static int csi_gpreg_hpll_freq = -1;
module_param(csi_gpreg_hpll_freq, int, 0644);
MODULE_PARM_DESC(csi_gpreg_hpll_freq,
		 "Override CSI GPREG HPLL frequency (-1 = log only)");

static int csi_gpreg_isclk_ratio = -1;
module_param(csi_gpreg_isclk_ratio, int, 0644);
MODULE_PARM_DESC(csi_gpreg_isclk_ratio,
		 "Override CSI GPREG ISCLK ratio (-1 = log only)");

static void ipu4p_isys_port_cfg(struct ipu_isys *isys)
{
	void __iomem *base = isys->pdata->base;
	void __iomem *isp_base = isys->adev->isp->base;
	u32 legacy_hpll, legacy_isclk, combo_hpll, combo_isclk;

	/*
	 * Windows CCsi::Prepare programs these two fields in both CSI GPREG
	 * banks before programming CR_PORT_CONFIG.  Keep the override disabled
	 * by default: the values are platform configuration, not settle-time
	 * tuning, and must not be guessed.
	 */
	if (csi_gpreg_hpll_freq >= 0 && csi_gpreg_isclk_ratio >= 0) {
		writel((u32)csi_gpreg_hpll_freq,
		       base + IPU_GPOFFSET + CSI2_REG_CSI_GPREG_HPLL_FREQ);
		writel((u32)csi_gpreg_isclk_ratio,
		       base + IPU_GPOFFSET + CSI2_REG_CSI_GPREG_ISCLK_RATIO);
		writel((u32)csi_gpreg_hpll_freq,
		       base + IPU_COMBO_GPOFFSET + CSI2_REG_CSI_GPREG_HPLL_FREQ);
		writel((u32)csi_gpreg_isclk_ratio,
		       base + IPU_COMBO_GPOFFSET + CSI2_REG_CSI_GPREG_ISCLK_RATIO);
		dev_info(&isys->adev->dev,
			 "trace isys gpreg clock override: hpll=0x%x isclk=0x%x\n",
			 (u32)csi_gpreg_hpll_freq, (u32)csi_gpreg_isclk_ratio);
	} else if (csi_gpreg_hpll_freq >= 0 || csi_gpreg_isclk_ratio >= 0) {
		dev_err(&isys->adev->dev,
			"trace isys gpreg clock override requires both values\n");
	}

	legacy_hpll = readl(base + IPU_GPOFFSET +
				    CSI2_REG_CSI_GPREG_HPLL_FREQ);
	legacy_isclk = readl(base + IPU_GPOFFSET +
				     CSI2_REG_CSI_GPREG_ISCLK_RATIO);
	combo_hpll = readl(base + IPU_COMBO_GPOFFSET +
				 CSI2_REG_CSI_GPREG_HPLL_FREQ);
	combo_isclk = readl(base + IPU_COMBO_GPOFFSET +
				  CSI2_REG_CSI_GPREG_ISCLK_RATIO);
	dev_info(&isys->adev->dev,
		 "trace isys gpreg clocks: legacy hpll=0x%x isclk=0x%x "
		 "combo hpll=0x%x isclk=0x%x\n",
		 legacy_hpll, legacy_isclk, combo_hpll, combo_isclk);

	/* Port config */
	writel(0x3895, base + IPU_GPOFFSET +
	       CSI2_REG_CSI_GPREG_CR_PORT_CONFIG);
	writel(0x3895, base + IPU_COMBO_GPOFFSET +
	       CSI2_REG_CSI_GPREG_CR_PORT_CONFIG);
	writel((0x100 << 1) | (0x100 << 10) | (0x100 << 19), isp_base +
		   BUTTRESS_REG_CSI_BSCAN_EXCLUDE);
	dev_info(&isys->adev->dev,
		 "trace isys port cfg: gpo=0x%x combo_gpo=0x%x bscan=0x%x\n",
		 readl(base + IPU_GPOFFSET + CSI2_REG_CSI_GPREG_CR_PORT_CONFIG),
		 readl(base + IPU_COMBO_GPOFFSET +
		       CSI2_REG_CSI_GPREG_CR_PORT_CONFIG),
		 readl(isp_base + BUTTRESS_REG_CSI_BSCAN_EXCLUDE));
}

void isys_setup_hw(struct ipu_isys *isys)
{
	dev_info(&isys->adev->dev, "trace isys setup: begin\n");
	ipu4p_isys_irq_cfg(isys);
	ipu4p_isys_port_cfg(isys);
	ipu4p_isys_bb_cfg(isys);
	ipu4p_isys_flush_idrain_en(isys);
	dev_info(&isys->adev->dev, "trace isys setup: complete\n");
}
#endif

#ifdef CONFIG_VIDEO_INTEL_IPU4
irqreturn_t isys_isr(struct ipu_bus_device *adev)
{
	struct ipu_isys *isys = ipu_bus_get_drvdata(adev);
	void __iomem *base = isys->pdata->base;
	u32 status;

	spin_lock(&isys->power_lock);
	if (!isys->power) {
		spin_unlock(&isys->power_lock);
		return IRQ_NONE;
	}

	status = readl(isys->pdata->base +
			   IPU_REG_ISYS_UNISPART_IRQ_STATUS);
	do {
		writel(status, isys->pdata->base +
			   IPU_REG_ISYS_UNISPART_IRQ_CLEAR);

		if (isys->isr_csi2_bits & status) {
			unsigned int i;

			for (i = 0; i < isys->pdata->ipdata->csi2.nports; i++) {
				if (IPU_ISYS_UNISPART_IRQ_CSI2(i) & status)
					ipu_isys_csi2_isr(&isys->csi2[i]);
			}
		}

		writel(0, base + IPU_REG_ISYS_UNISPART_SW_IRQ_REG);

		/*
		 * Handle a single FW event per checking the CSI-2
		 * receiver SOF status. This is done in order to avoid
		 * the case where events arrive to the event queue and
		 * one of them is a SOF event which then could be
		 * handled before the SOF interrupt. This would pose
		 * issues in sequence numbering which is based on SOF
		 * interrupts, always assumed to arrive before FW SOF
		 * events.
		 */
		if (status & IPU_ISYS_UNISPART_IRQ_SW && !isys_isr_one(adev))
			status = IPU_ISYS_UNISPART_IRQ_SW;
		else
			status = 0;

		status |= readl(isys->pdata->base +
				    IPU_REG_ISYS_UNISPART_IRQ_STATUS);
	} while (status & (isys->isr_csi2_bits
			   | IPU_ISYS_UNISPART_IRQ_SW) &&
		 !isys->adev->isp->flr_done);
	spin_unlock(&isys->power_lock);

	return IRQ_HANDLED;
}
#endif

#ifdef CONFIG_VIDEO_INTEL_IPU4P
irqreturn_t isys_isr(struct ipu_bus_device *adev)
{
	struct ipu_isys *isys = ipu_bus_get_drvdata(adev);
	void __iomem *base = isys->pdata->base;
	u32 status;
	unsigned int i;
	u32 sip0_status, sip1_status;
	struct {
		u32 *status;
		u32 mask;
	} csi2_irq_mask[] = {
		{&sip0_status, IPU_ISYS_CSI2_D_IRQ_MASK},
		{&sip1_status, IPU_ISYS_CSI2_A_IRQ_MASK},
		{&sip1_status, IPU_ISYS_CSI2_B_IRQ_MASK},
		{&sip1_status, IPU_ISYS_CSI2_C_IRQ_MASK},
		{&sip1_status, IPU_ISYS_CSI2_D_IRQ_MASK},
		{&sip1_status, IPU_ISYS_CSI2_E_IRQ_MASK},
		{&sip1_status, IPU_ISYS_CSI2_F_IRQ_MASK},
		{&sip1_status, IPU_ISYS_CSI2_G_IRQ_MASK},
	};

	spin_lock(&isys->power_lock);
	if (!isys->power) {
		spin_unlock(&isys->power_lock);
		return IRQ_NONE;
	}

	/* read unis sw irq */
	status = readl(isys->pdata->base +
			   IPU_REG_ISYS_UNISPART_IRQ_STATUS);
	dev_dbg(&adev->dev, "isys irq status - unis sw irq = 0x%x", status);

	do {
		/* clear unis sw irqs */
		writel(status, isys->pdata->base +
			   IPU_REG_ISYS_UNISPART_IRQ_CLEAR);

		/* read and clear sip irq status */
		sip0_status = readl(isys->pdata->base +
					IPU_REG_ISYS_SIP0_IRQ_CTRL_STATUS);
		sip1_status = readl(isys->pdata->base +
					IPU_REG_ISYS_SIP1_IRQ_CTRL_STATUS);
		dev_dbg(&adev->dev, "isys irq status - sip0 = 0x%x sip1 = 0x%x",
			sip0_status, sip1_status);
		writel(sip0_status, isys->pdata->base +
			   IPU_REG_ISYS_SIP0_IRQ_CTRL_CLEAR);
		writel(sip1_status, isys->pdata->base +
			   IPU_REG_ISYS_SIP1_IRQ_CTRL_CLEAR);

		for (i = 0; i < isys->pdata->ipdata->csi2.nports; i++) {
			if (*csi2_irq_mask[i].status & csi2_irq_mask[i].mask)
				ipu_isys_csi2_isr(&isys->csi2[i]);
		}

		writel(0, base + IPU_REG_ISYS_UNISPART_SW_IRQ_REG);

		/*
		 * Handle a single FW event per checking the CSI-2
		 * receiver SOF status. This is done in order to avoid
		 * the case where events arrive to the event queue and
		 * one of them is a SOF event which then could be
		 * handled before the SOF interrupt. This would pose
		 * issues in sequence numbering which is based on SOF
		 * interrupts, always assumed to arrive before FW SOF
		 * events.
		 */
		if (status & IPU_ISYS_UNISPART_IRQ_SW && !isys_isr_one(adev))
			status = IPU_ISYS_UNISPART_IRQ_SW;
		else
			status = 0;

		status |= readl(isys->pdata->base +
				    IPU_REG_ISYS_UNISPART_IRQ_STATUS);
	} while (status & (isys->isr_csi2_bits
			   | IPU_ISYS_UNISPART_IRQ_SW) &&
		 !isys->adev->isp->flr_done);
	spin_unlock(&isys->power_lock);

	return IRQ_HANDLED;
}
#endif

int tpg_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ipu_isys_tpg *tpg = to_ipu_isys_tpg(sd);
#ifdef IPU_VC_SUPPORT
	__u32 code = tpg->asd.ffmt[TPG_PAD_SOURCE][0].code;
#else
	__u32 code = tpg->asd.ffmt[TPG_PAD_SOURCE].code;
#endif
	unsigned int bpp = ipu_isys_mbus_code_to_bpp(code);

	/*
	 * MIPI_GEN block is CSI2 FB. Need to enable/disable TPG selection
	 * register to control the TPG streaming.
	 */
	if (tpg->sel)
		writel(enable ? 1 : 0, tpg->sel);

	if (!enable) {
		writel(0, tpg->base + MIPI_GEN_REG_COM_ENABLE);
		return 0;
	}

	writel(MIPI_GEN_COM_DTYPE_RAW(bpp),
		   tpg->base + MIPI_GEN_REG_COM_DTYPE);
	writel(ipu_isys_mbus_code_to_mipi(code),
		   tpg->base + MIPI_GEN_REG_COM_VTYPE);
	writel(0, tpg->base + MIPI_GEN_REG_COM_VCHAN);

	writel(0, tpg->base + MIPI_GEN_REG_SYNG_NOF_FRAMES);

#ifdef IPU_VC_SUPPORT
	writel(DIV_ROUND_UP(tpg->asd.ffmt[TPG_PAD_SOURCE][0].width *
				bpp, BITS_PER_BYTE),
		   tpg->base + MIPI_GEN_REG_COM_WCOUNT);
	writel(DIV_ROUND_UP(tpg->asd.ffmt[TPG_PAD_SOURCE][0].width,
				MIPI_GEN_PPC),
		   tpg->base + MIPI_GEN_REG_SYNG_NOF_PIXELS);
	writel(tpg->asd.ffmt[TPG_PAD_SOURCE][0].height,
		   tpg->base + MIPI_GEN_REG_SYNG_NOF_LINES);
#else
	writel(DIV_ROUND_UP(tpg->asd.ffmt[TPG_PAD_SOURCE].width *
				bpp, BITS_PER_BYTE),
		   tpg->base + MIPI_GEN_REG_COM_WCOUNT);
	writel(DIV_ROUND_UP(tpg->asd.ffmt[TPG_PAD_SOURCE].width,
				MIPI_GEN_PPC),
		   tpg->base + MIPI_GEN_REG_SYNG_NOF_PIXELS);
	writel(tpg->asd.ffmt[TPG_PAD_SOURCE].height,
		   tpg->base + MIPI_GEN_REG_SYNG_NOF_LINES);
#endif

	writel(0, tpg->base + MIPI_GEN_REG_TPG_MODE);
	writel(-1, tpg->base + MIPI_GEN_REG_TPG_HCNT_MASK);
	writel(-1, tpg->base + MIPI_GEN_REG_TPG_VCNT_MASK);
	writel(-1, tpg->base + MIPI_GEN_REG_TPG_XYCNT_MASK);
	writel(0, tpg->base + MIPI_GEN_REG_TPG_HCNT_DELTA);
	writel(0, tpg->base + MIPI_GEN_REG_TPG_VCNT_DELTA);

	v4l2_ctrl_handler_setup(&tpg->asd.ctrl_handler);

	writel(2, tpg->base + MIPI_GEN_REG_COM_ENABLE);
	return 0;
}
