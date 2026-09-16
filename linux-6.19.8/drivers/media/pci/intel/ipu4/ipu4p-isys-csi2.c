// SPDX-License-Identifier: GPL-2.0
//  Copyright (C) 2018 Intel Corporation

#include "ipu.h"
#include "ipu-buttress.h"
#include "ipu-isys.h"
#include "ipu-isys-csi2.h"
#include "ipu-platform.h"
#include "ipu-platform-buttress-regs.h"
#include "ipu-platform-isys-csi2-reg.h"
#include "ipu-platform-regs.h"
#include "ipu-trace.h"
#include "ipu-isys-csi2.h"

#define CSI2_UPDATE_TIME_TRY_NUM   3
#define CSI2_UPDATE_TIME_MAX_DIFF  20

static int ipu4p_csi2_ev_correction_params(struct ipu_isys_csi2
						   *csi2, unsigned int lanes)
{
	/*
	 * TBD: add implementation for ipu4p
	 * probably re-use ipu4 implementation
	 */
	return 0;
}

/* IPU4P's compact receiver index is not its firmware source number. */
static bool ipu4p_csi2_apply_source7_mipi_timing(struct ipu_isys_csi2 *csi2)
{
	const struct ipu4p_isys_quirks *quirks;
	const struct ipu4p_mipi_receiver_timing *mipi_timing;
	unsigned int i;

	quirks = ipu4p_isys_get_quirks(&csi2->isys->adev->dev);
	if (!quirks || csi2->index != quirks->front_csi_index ||
	    csi2->asd.source != IPU_FW_ISYS_STREAM_SRC_CSI2_PORT0 +
	    quirks->front_source || csi2->nlanes != quirks->front_lanes)
		return false;
	mipi_timing = &quirks->front_mipi_timing;

	/*
	 * On the Surface Pro 7 IPU4P front path (OV5693, CSI-2 index 2),
	 * Linux-calculated timing produces receiver_errors=0x683 and zero
	 * frames. Windows ConfigMipiClk instead uses the rate record above:
	 * its 350 MHz input produces the clock/first-data and data receiver
	 * counter values used below.
	 * Keep this quirk local to the IPU4P source-7 implementation; all
	 * other sources retain the generic calculated timing below.
	 */
	writel(0, csi2->base + 0x30);
	writel(mipi_timing->clock_first_data_ticks, csi2->base + 0x34);
	for (i = 0; i < 8; i++) {
		writel(0, csi2->base + 0x38 + i * 8);
		writel(mipi_timing->data_ticks, csi2->base + 0x3c + i * 8);
	}

	dev_dbg(&csi2->isys->adev->dev,
		"source-7 MIPI timing quirk applied: rate=%u Hz "
		"clock/first-data=%u ticks data=%u ticks\n",
		mipi_timing->receiver_frequency_hz,
		mipi_timing->clock_first_data_ticks, mipi_timing->data_ticks);

	return true;
}


static void ipu4p_csi2_log_rx_state(struct ipu_isys_csi2 *csi2, const char *tag)
{
	unsigned long flags;
	u32 receiver_errors, last_receiver_errors, fatal_receiver_errors;
	u32 enable = readl(csi2->base + CSI2_REG_CSI_RX_ENABLE);
	u32 lanes = readl(csi2->base + CSI2_REG_CSI_RX_NOF_ENABLED_LANES);
	u32 config = readl(csi2->base + CSI2_REG_CSI_RX_CONFIG);
	u32 status = readl(csi2->base + CSI2_REG_CSI_RX_STATUS);
	u32 hs = readl(csi2->base + CSI2_REG_CSI_RX_STATUS_DLANE_HS);
	u32 lp = readl(csi2->base + CSI2_REG_CSI_RX_STATUS_DLANE_LP);
	u32 ctermen = readl(csi2->base + CSI2_REG_CSI_RX_DLY_CNT_TERMEN_CLANE);
	u32 csettle = readl(csi2->base + CSI2_REG_CSI_RX_DLY_CNT_SETTLE_CLANE);
	u32 dtermen0 = readl(csi2->base + CSI2_REG_CSI_RX_DLY_CNT_TERMEN_DLANE(0));
	u32 dsettle0 = readl(csi2->base + CSI2_REG_CSI_RX_DLY_CNT_SETTLE_DLANE(0));
	u32 dtermen1 = readl(csi2->base + CSI2_REG_CSI_RX_DLY_CNT_TERMEN_DLANE(1));
	u32 dsettle1 = readl(csi2->base + CSI2_REG_CSI_RX_DLY_CNT_SETTLE_DLANE(1));

	spin_lock_irqsave(&csi2->receiver_error_lock, flags);
	receiver_errors = csi2->receiver_errors;
	last_receiver_errors = csi2->last_receiver_errors;
	fatal_receiver_errors = csi2->fatal_receiver_errors;
	spin_unlock_irqrestore(&csi2->receiver_error_lock, flags);

	dev_dbg(&csi2->isys->adev->dev,
		"csi %u %s: rx enable=0x%x lanes=%u config=0x%x "
		"status=0x%x hs=0x%x lp=0x%x "
		"ctermen=%u csettle=%u d0termen=%u d0settle=%u "
		"d1termen=%u d1settle=%u receiver_errors=0x%x "
		"last_receiver_errors=0x%x fatal_receiver_errors=0x%x\n",
		csi2->index, tag, enable, lanes, config, status, hs, lp,
		ctermen, csettle, dtermen0, dsettle0, dtermen1, dsettle1,
		receiver_errors, last_receiver_errors, fatal_receiver_errors);
}

static void ipu4p_isys_register_errors(struct ipu_isys_csi2 *csi2)
{
	u32 status;
	unsigned int index;
	unsigned long flags;
	struct ipu_isys *isys = csi2->isys;
	void __iomem *isys_base = isys->pdata->base;

	index = csi2->index;
	spin_lock_irqsave(&csi2->receiver_error_lock, flags);
	status = readl(isys_base +
			   IPU_REG_ISYS_CSI_IRQ_CTRL0_BASE(index) + 0x8);
	writel(status, isys_base +
		   IPU_REG_ISYS_CSI_IRQ_CTRL0_BASE(index) + 0xc);

	status &= 0xffff;
	csi2->receiver_errors |= status;
	spin_unlock_irqrestore(&csi2->receiver_error_lock, flags);
	dev_dbg(&isys->adev->dev, "csi %d rxsync status 0x%x", index, status);
}

int ipu_isys_csi2_error(struct ipu_isys_csi2 *csi2)
{
	/*
	 * Strings corresponding to CSI-2 receiver errors are here.
	 * Corresponding macros are defined in the header file.
	 */
	static const struct ipu_isys_csi2_error {
		const char *error_string;
		bool is_info_only;
	} errors[] = {
		{"Single packet header error corrected", true},
		{"Multiple packet header errors detected", true},
		{"Payload checksum (CRC) error", true},
		{"FIFO overflow", false},
		{"Reserved short packet data type detected", true},
		{"Reserved long packet data type detected", true},
		{"Incomplete long packet detected", false},
		{"Frame sync error", false},
		{"Line sync error", false},
		{"DPHY recoverable synchronization error", true},
		{"DPHY non-recoverable synchronization error", false},
		{"Escape mode error", true},
		{"Escape mode trigger event", true},
		{"Escape mode ultra-low power state for data lane(s)", true},
		{"Escape mode ultra-low power state exit for clock lane", true},
		{"Inter-frame short packet discarded", true},
		{"Inter-frame long packet discarded", true},
	};
	u32 status;
	unsigned long flags;
	unsigned int i;

	/* Register errors once more in case of error interrupts are disabled */
	ipu4p_isys_register_errors(csi2);
	ipu4p_csi2_log_rx_state(csi2, "error snapshot");
	spin_lock_irqsave(&csi2->receiver_error_lock, flags);
	status = csi2->receiver_errors;
	csi2->receiver_errors = 0;
	csi2->last_receiver_errors = status;
	csi2->fatal_receiver_errors |= status & IPU_ISYS_CSI2_FATAL_ERRORS;
	spin_unlock_irqrestore(&csi2->receiver_error_lock, flags);
	if (status)
		dev_err_ratelimited(&csi2->isys->adev->dev,
				    "csi2-%i receiver error status 0x%x%s\n",
				    csi2->index, status,
				    status & IPU_ISYS_CSI2_FATAL_ERRORS ?
				    " (fatal)" : "");

	for (i = 0; i < ARRAY_SIZE(errors); i++) {
		if (status & BIT(i)) {
			if (errors[i].is_info_only)
				dev_dbg(&csi2->isys->adev->dev,
					"csi2-%i info: %s\n",
					csi2->index, errors[i].error_string);
			else
				dev_err_ratelimited(&csi2->isys->adev->dev,
						    "csi2-%i error: %s\n",
						    csi2->index,
						    errors[i].error_string);
		}
	}

	return (status & IPU_ISYS_CSI2_FATAL_ERRORS) ? -EIO : 0;
}

void ipu_isys_csi2_reset_errors(struct ipu_isys_csi2 *csi2)
{
	void __iomem *isys_base = csi2->isys->pdata->base;
	u32 status;
	unsigned long flags;

	spin_lock_irqsave(&csi2->receiver_error_lock, flags);
	status = readl(isys_base +
		       IPU_REG_ISYS_CSI_IRQ_CTRL_BASE(csi2->index) + 0x8);
	writel(status, isys_base +
	       IPU_REG_ISYS_CSI_IRQ_CTRL_BASE(csi2->index) + 0xc);
	status = readl(isys_base +
		       IPU_REG_ISYS_CSI_IRQ_CTRL0_BASE(csi2->index) + 0x8);
	writel(status, isys_base +
	       IPU_REG_ISYS_CSI_IRQ_CTRL0_BASE(csi2->index) + 0xc);
	csi2->receiver_errors = 0;
	csi2->last_receiver_errors = 0;
	csi2->fatal_receiver_errors = 0;
	spin_unlock_irqrestore(&csi2->receiver_error_lock, flags);
}

int ipu_isys_csi2_set_stream(struct v4l2_subdev *sd,
			     struct ipu_isys_csi2_timing timing,
			     unsigned int nlanes, int enable)
{
	struct ipu_isys_csi2 *csi2 = to_ipu_isys_csi2(sd);
	struct ipu_isys *isys = csi2->isys;
	void __iomem *isys_base = isys->pdata->base;
	unsigned int i;
	u32 val, csi2part = 0;

	dev_dbg(&csi2->isys->adev->dev, "csi2 s_stream %d\n", enable);
	ipu4p_csi2_log_rx_state(csi2, "set_stream entry");
	if (!enable) {
		ipu4p_csi2_log_rx_state(csi2, "set_stream disable pre-error");
		ipu_isys_csi2_error(csi2);

		val = readl(csi2->base + CSI2_REG_CSI_RX_CONFIG);
		val &= ~(CSI2_CSI_RX_CONFIG_DISABLE_BYTE_CLK_GATING |
			 CSI2_CSI_RX_CONFIG_RELEASE_LP11);
		writel(val, csi2->base + CSI2_REG_CSI_RX_CONFIG);

		writel(0, csi2->base + CSI2_REG_CSI_RX_ENABLE);

		writel(0, isys_base +
			   IPU_REG_ISYS_CSI_IRQ_CTRL_BASE(csi2->index) + 0x4);
		writel(0, isys_base +
			   IPU_REG_ISYS_CSI_IRQ_CTRL_BASE(csi2->index) +
			   0x10);
		writel
		    (0, isys_base +
		     IPU_REG_ISYS_CSI_IRQ_CTRL0_BASE(csi2->index) + 0x4);
		writel
		    (0, isys_base +
		     IPU_REG_ISYS_CSI_IRQ_CTRL0_BASE(csi2->index) + 0x10);
		ipu4p_csi2_log_rx_state(csi2, "set_stream disable done");
		ipu_isys_csi2_reset_errors(csi2);
		return 0;
	}

	ipu4p_csi2_ev_correction_params(csi2, nlanes);

	writel(timing.ctermen,
		   csi2->base + CSI2_REG_CSI_RX_DLY_CNT_TERMEN_CLANE);
	writel(timing.csettle,
		   csi2->base + CSI2_REG_CSI_RX_DLY_CNT_SETTLE_CLANE);

	if (!ipu4p_csi2_apply_source7_mipi_timing(csi2)) {
		for (i = 0; i < nlanes; i++) {
			writel
			    (timing.dtermen,
			     csi2->base + CSI2_REG_CSI_RX_DLY_CNT_TERMEN_DLANE(i));
			writel
			    (timing.dsettle,
			     csi2->base + CSI2_REG_CSI_RX_DLY_CNT_SETTLE_DLANE(i));
		}
	}

	/*
	 * Windows' source-7 receiver path programs the lane count before
	 * RX_CONFIG, then enables the receiver. Keep that ordering here so the
	 * combo receiver sees the same configuration sequence.
	 */
	writel(nlanes, csi2->base + CSI2_REG_CSI_RX_NOF_ENABLED_LANES);

	val = readl(csi2->base + CSI2_REG_CSI_RX_CONFIG);
	val |= CSI2_CSI_RX_CONFIG_DISABLE_BYTE_CLK_GATING |
	    CSI2_CSI_RX_CONFIG_RELEASE_LP11;
	writel(val, csi2->base + CSI2_REG_CSI_RX_CONFIG);

	writel(CSI2_CSI_RX_ENABLE_ENABLE,
		   csi2->base + CSI2_REG_CSI_RX_ENABLE);

#ifdef IPU_VC_SUPPORT
	/* SOF of VC0-VC3 enabled from CSI2PART register in B0 */
	for (i = 0; i < NR_OF_CSI2_VC; i++)
		csi2part |= CSI2_IRQ_FS_VC(i) | CSI2_IRQ_FE_VC(i);
#else
	csi2part |= CSI2_IRQ_FS_VC | CSI2_IRQ_FE_VC;
#endif

	/* Enable csi2 receiver error interrupts */
	writel(1, isys_base +
		   IPU_REG_ISYS_CSI_IRQ_CTRL_BASE(csi2->index));
	writel(0, isys_base +
		   IPU_REG_ISYS_CSI_IRQ_CTRL_BASE(csi2->index) + 0x14);
	writel(0xffffffff, isys_base +
		   IPU_REG_ISYS_CSI_IRQ_CTRL_BASE(csi2->index) + 0xc);
	writel(1, isys_base +
		   IPU_REG_ISYS_CSI_IRQ_CTRL_BASE(csi2->index) + 0x4);
	writel(1, isys_base +
		   IPU_REG_ISYS_CSI_IRQ_CTRL_BASE(csi2->index) + 0x10);

	csi2part |= 0xffff;
	writel(csi2part, isys_base +
		   IPU_REG_ISYS_CSI_IRQ_CTRL0_BASE(csi2->index));
	writel(0, isys_base +
		   IPU_REG_ISYS_CSI_IRQ_CTRL0_BASE(csi2->index) + 0x14);
	writel(0xffffffff, isys_base +
		   IPU_REG_ISYS_CSI_IRQ_CTRL0_BASE(csi2->index) + 0xc);
	writel(csi2part, isys_base +
		   IPU_REG_ISYS_CSI_IRQ_CTRL0_BASE(csi2->index) + 0x4);
	writel(csi2part, isys_base +
		   IPU_REG_ISYS_CSI_IRQ_CTRL0_BASE(csi2->index) + 0x10);

	ipu4p_csi2_log_rx_state(csi2, "set_stream enable done");
	dev_dbg(&csi2->isys->adev->dev,
		 "csi %u stream enabled: lanes=%u timing ctermen=%u "
		 "csettle=%u dtermen=%u dsettle=%u source=%u\n",
		 csi2->index, nlanes, timing.ctermen, timing.csettle,
		 timing.dtermen, timing.dsettle, csi2->asd.source);
	return 0;
}

void ipu_isys_csi2_isr(struct ipu_isys_csi2 *csi2)
{
	u32 status = 0;
	u32 ctrl_status;
	unsigned long flags;
#ifdef IPU_VC_SUPPORT
	unsigned int i, bus;
#else
	unsigned int bus;
#endif
	struct ipu_isys *isys = csi2->isys;
	void __iomem *isys_base = isys->pdata->base;

	bus = csi2->index;
	spin_lock_irqsave(&csi2->receiver_error_lock, flags);
	/* handle ctrl and ctrl0 irq */
	ctrl_status = readl(isys_base +
			   IPU_REG_ISYS_CSI_IRQ_CTRL_BASE(bus) + 0x8);
	writel(ctrl_status, isys_base +
		   IPU_REG_ISYS_CSI_IRQ_CTRL_BASE(bus) + 0xc);

	if (!(ctrl_status & BIT(0))) {
		spin_unlock_irqrestore(&csi2->receiver_error_lock, flags);
		dev_dbg(&isys->adev->dev, "csi %d irq_ctrl status 0x%x",
			bus, ctrl_status);
		return;
	}

	status = readl(isys_base +
			   IPU_REG_ISYS_CSI_IRQ_CTRL0_BASE(bus) + 0x8);
	writel(status, isys_base +
		   IPU_REG_ISYS_CSI_IRQ_CTRL0_BASE(bus) + 0xc);
	/* register the csi sync error */
	csi2->receiver_errors |= status & 0xffff;
	spin_unlock_irqrestore(&csi2->receiver_error_lock, flags);
	dev_dbg(&isys->adev->dev, "csi %d irq_ctrl status 0x%x", bus,
		ctrl_status);
	dev_dbg(&isys->adev->dev, "csi %d irq_ctrl0 status 0x%x", bus,
		status);
	/* handle sof and eof event */
#ifdef IPU_VC_SUPPORT
	for (i = 0; i < NR_OF_CSI2_VC; i++) {
		if (status & CSI2_IRQ_FS_VC(i))
			ipu_isys_csi2_sof_event(csi2, i);

		if (status & CSI2_IRQ_FE_VC(i))
			ipu_isys_csi2_eof_event(csi2, i);
	}
#else
	if (status & CSI2_IRQ_FS_VC)
		ipu_isys_csi2_sof_event(csi2);
	if (status & CSI2_IRQ_FE_VC)
		ipu_isys_csi2_eof_event(csi2);
#endif
}

static u64 tunit_time_to_us(struct ipu_isys *isys, u64 time)
{
	struct ipu_bus_device *adev = to_ipu_bus_device(isys->adev->iommu);
	u64 isys_clk = IS_FREQ_SOURCE / adev->ctrl->divisor / 1000000;

	do_div(time, isys_clk);

	return time;
}

static u64 tsc_time_to_tunit_time(struct ipu_isys *isys,
				  u64 tsc_base, u64 tunit_base, u64 tsc_time)
{
	struct ipu_bus_device *adev = to_ipu_bus_device(isys->adev->iommu);
	u64 isys_clk = IS_FREQ_SOURCE / adev->ctrl->divisor / 100000;
	u64 tsc_clk = IPU_BUTTRESS_TSC_CLK / 100000;

	tsc_time *= isys_clk;
	tsc_base *= isys_clk;
	do_div(tsc_time, tsc_clk);
	do_div(tsc_base, tsc_clk);

	return tunit_base + tsc_time - tsc_base;
}

static int update_timer_base(struct ipu_isys *isys)
{
	int rval, i;
	u64 time;

	for (i = 0; i < CSI2_UPDATE_TIME_TRY_NUM; i++) {
		rval = ipu_trace_get_timer(&isys->adev->dev, &time);
		if (rval) {
			dev_err(&isys->adev->dev,
				"Failed to read Tunit timer.\n");
			return rval;
		}
		rval = ipu4_buttress_tsc_read(isys->adev->isp,
					     &isys->tsc_timer_base);
		if (rval) {
			dev_err(&isys->adev->dev,
				"Failed to read TSC timer.\n");
			return rval;
		}
		rval = ipu_trace_get_timer(&isys->adev->dev,
					   &isys->tunit_timer_base);
		if (rval) {
			dev_err(&isys->adev->dev,
				"Failed to read Tunit timer.\n");
			return rval;
		}
		if (tunit_time_to_us(isys, isys->tunit_timer_base - time) <
		    CSI2_UPDATE_TIME_MAX_DIFF)
			return 0;
	}
	dev_dbg(&isys->adev->dev, "Timer base values may not be accurate.\n");
	return 0;
}

/* Extract the timestamp from trace message.
 * The timestamp in the traces message contains two parts.
 * The lower part contains bit0 ~ 15 of the total 64bit timestamp.
 * The higher part contains bit14 ~ 63 of the 64bit timestamp.
 * These two parts are sampled at different time.
 * Two overlaped bits are used to identify if there's roll overs
 * in the lower part during the two samples.
 * If the two overlapped bits do not match, a fix is needed to
 * handle the roll over.
 */
static u64 extract_time_from_short_packet_msg(struct
					      ipu_isys_csi2_monitor_message
					      *msg)
{
	u64 time_h = msg->timestamp_h << 14;
	u64 time_l = msg->timestamp_l;
	u64 time_h_ovl = time_h & 0xc000;
	u64 time_h_h = time_h & (~0xffff);

	/* Fix possible roll overs. */
	if (time_h_ovl >= (time_l & 0xc000))
		return time_h_h | time_l;
	else
		return (time_h_h - 0x10000) | time_l;
}

unsigned int ipu_isys_csi2_get_current_field(struct ipu_isys_pipeline *ip,
					     unsigned int *timestamp)
{
	struct ipu_isys_video *av = container_of(ip, struct ipu_isys_video, ip);
	struct ipu_isys *isys = av->isys;
	unsigned int field = V4L2_FIELD_TOP;

	/*
	 * Find the nearest message that has matched msg type,
	 * port id, virtual channel and packet type.
	 */
	unsigned int i = ip->short_packet_trace_index;
	bool msg_matched = false;
	unsigned int monitor_id;

	update_timer_base(isys);

	if (ip->csi2->index >= IPU_ISYS_MAX_CSI2_LEGACY_PORTS)
		monitor_id = TRACE_REG_CSI2_3PH_TM_MONITOR_ID;
	else
		monitor_id = TRACE_REG_CSI2_TM_MONITOR_ID;

	dma_sync_single_for_cpu(&isys->adev->dev,
				isys->short_packet_trace_buffer_dma_addr,
				IPU_ISYS_SHORT_PACKET_TRACE_BUFFER_SIZE,
				DMA_BIDIRECTIONAL);

	do {
		struct ipu_isys_csi2_monitor_message msg =
		    isys->short_packet_trace_buffer[i];
		u64 sof_time = tsc_time_to_tunit_time(isys,
						      isys->tsc_timer_base,
						      isys->tunit_timer_base,
						      (((u64) timestamp[1]) <<
						       32) | timestamp[0]);
		u64 trace_time = extract_time_from_short_packet_msg(&msg);
		u64 delta_time_us = tunit_time_to_us(isys,
						     (sof_time > trace_time) ?
						     sof_time - trace_time :
						     trace_time - sof_time);

		i = (i + 1) % IPU_ISYS_SHORT_PACKET_TRACE_MSG_NUMBER;

		if (msg.cmd == TRACE_REG_CMD_TYPE_D64MTS &&
		    msg.monitor_id == monitor_id &&
		    msg.fs == 1 &&
		    msg.port == ip->csi2->index &&
#ifdef IPU_VC_SUPPORT
		    msg.vc == ip->vc &&
#endif
		    delta_time_us < IPU_ISYS_SHORT_PACKET_TRACE_MAX_TIMESHIFT) {
			field = (msg.sequence % 2) ?
			    V4L2_FIELD_TOP : V4L2_FIELD_BOTTOM;
			ip->short_packet_trace_index = i;
			msg_matched = true;
			dev_dbg(&isys->adev->dev,
				"Interlaced field ready. field = %d\n", field);
			break;
		}
	} while (i != ip->short_packet_trace_index);
	if (!msg_matched)
		/* We have walked through the whole buffer. */
		dev_dbg(&isys->adev->dev, "No matched trace message found.\n");

	return field;
}

bool ipu_isys_csi2_skew_cal_required(struct ipu_isys_csi2 *csi2)
{
	__s64 link_freq;
	int rval;

	if (!csi2)
		return false;

#ifdef IPU_VC_SUPPORT
	/* Not yet ? */
	if (csi2->remote_streams != csi2->stream_count)
		return false;

#endif
	rval = ipu_isys_csi2_get_link_freq(csi2, &link_freq);
	if (rval)
		return false;

	if (link_freq <= IPU_SKEW_CAL_LIMIT_HZ)
		return false;

	return true;
}

int ipu_isys_csi2_set_skew_cal(struct ipu_isys_csi2 *csi2, int enable)
{
	u32 val;

	val = readl(csi2->base + CSI2_REG_CSI_RX_CONFIG);

	if (enable)
		val |= CSI2_CSI_RX_CONFIG_SKEWCAL_ENABLE;
	else
		val &= ~CSI2_CSI_RX_CONFIG_SKEWCAL_ENABLE;

	writel(val, csi2->base + CSI2_REG_CSI_RX_CONFIG);

	return 0;
}
