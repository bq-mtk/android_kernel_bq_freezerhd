/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifdef BUILD_UBOOT
#define ENABLE_DSI_INTERRUPT 0

#include <asm/arch/disp_drv_platform.h>
#else

#define ENABLE_DSI_INTERRUPT 1

#include <linux/delay.h>
#include <disp_drv_log.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include "disp_drv_platform.h"

#include "lcd_reg.h"
#include "lcd_drv.h"

#include "dsi_reg.h"
#include "dsi_drv.h"
#endif


#if ENABLE_DSI_INTERRUPT
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include "mtkfb.h"
#include "fbconfig_kdebug.h"
static wait_queue_head_t _dsi_wait_queue;
static wait_queue_head_t _dsi_dcs_read_wait_queue;
static wait_queue_head_t _dsi_wait_bta_te;
static wait_queue_head_t _dsi_wait_ext_te;
static wait_queue_head_t _dsi_wait_vm_done_queue;
#endif
/* static unsigned int _dsi_reg_update_wq_flag = 0; */
static DECLARE_WAIT_QUEUE_HEAD(_dsi_reg_update_wq);

#include "disp_debug.h"

/* #define ENABLE_DSI_ERROR_REPORT */

/*
#define PLL_BASE            (0xF0060000)
#define DSI_PHY_BASE        (0xF0060B00)
#define DSI_BASE                (0xF0140000)
*/

static PDSI_REGS DSI_REG;
static PDSI_VM_CMDQ_REGS DSI_VM_CMD_REG;
static PDSI_PHY_REGS DSI_PHY_REG;
static PDSI_CMDQ_REGS DSI_CMDQ_REG;
/* static PLCD_REGS const LCD_REG = (PLCD_REGS)(LCD_BASE); */



static bool dsi_log_on;
static bool glitch_log_on;
static bool force_transfer;

struct DSI_CONTEXT {
	DSI_REGS regBackup;
	unsigned int cmdq_size;
	DSI_CMDQ_REGS cmdqBackup;
	unsigned int bit_time_ns;
	unsigned int vfp_period_us;
	unsigned int vsa_vs_period_us;
	unsigned int vsa_hs_period_us;
	unsigned int vsa_ve_period_us;
	unsigned int vbp_period_us;
	void (*pIntCallback)(DISP_INTERRUPT_EVENTS);
};
#define DSI_CONTEXT    struct DSI_CONTEXT


static bool s_isDsiPowerOn;
static DSI_CONTEXT _dsiContext;

/* PLL_clock * 10 */
#define PLL_TABLE_NUM  (26)
int LCM_DSI_6582_PLL_CLOCK_List[PLL_TABLE_NUM + 1] = {
	0, 1000, 1040, 1250, 1300, 1500, 1560, 1750, 1820, 2000, 2080, 2250,
	2340, 2500, 2600, 2750, 2860, 3000, 3120, 3250, 3500, 3750, 4000, 4250, 4500, 4750, 5000
};

DSI_PLL_CONFIG pll_config[PLL_TABLE_NUM + 1] = { {0, 0, 0, 0, 0, 0, 0},
{2, 0, 0x3D89D89D, 1, 0x1B1, 0x745, 0x745},
{2, 0, 0x40000000, 1, 0x1B1, 0x790, 0x790},
{1, 0, 0x26762762, 1, 0x1B1, 0x48B, 0x48B},
{1, 0, 0x28000000, 1, 0x1B1, 0x4BA, 0x4BA},
{1, 0, 0x2E276276, 1, 0x1B1, 0x574, 0x574},
{1, 0, 0x30000000, 1, 0x1B1, 0x5AC, 0x5AC},
{1, 0, 0x35D89D89, 1, 0x1B1, 0x65D, 0x65D},
{1, 0, 0x38000000, 1, 0x1B1, 0x69E, 0x69E},
{1, 0, 0x3D89D89D, 1, 0x1B1, 0x745, 0x745},
{1, 0, 0x40000000, 1, 0x1B1, 0x790, 0x790},
{1, 0, 0x453B13B1, 1, 0x1B1, 0x82E, 0x82E},
{1, 0, 0x48000000, 1, 0x1B1, 0x882, 0x882},
{0, 0, 0x26762762, 1, 0x1B1, 0x48B, 0x48B},
{0, 0, 0x28000000, 1, 0x1B1, 0x4BA, 0x4BA},
{0, 0, 0x2A4EC4EC, 1, 0x1B1, 0x500, 0x500},
{0, 0, 0x2C000000, 1, 0x1B1, 0x533, 0x533},
{0, 0, 0x2E276276, 1, 0x1B1, 0x574, 0x574},
{0, 0, 0x30000000, 1, 0x1B1, 0x5AC, 0x5AC},
{0, 0, 0x32000000, 1, 0x1B1, 0x5E8, 0x5E8},
{0, 0, 0x35D89D89, 1, 0x1B1, 0x65D, 0x65D},
{0, 0, 0x39B13B13, 1, 0x1B1, 0x6D1, 0x6D1},
{0, 0, 0x3D89D89D, 1, 0x1B1, 0x745, 0x745},
{0, 0, 0x41627627, 1, 0x1B1, 0x7BA, 0x7BA},
{0, 0, 0x453B13B1, 1, 0x1B1, 0x82E, 0x82E},
{0, 0, 0x4913B13B, 1, 0x1B1, 0x8A2, 0x8A2},
{0, 0, 0x4CEC4EC4, 1, 0x1B1, 0x917, 0x917}
};

#ifndef BUILD_UBOOT

#ifndef MT65XX_NEW_DISP
static bool dsi_esd_recovery;
static bool dsi_int_te_enabled;
static unsigned int dsi_int_te_period = 1;
static unsigned int dsi_dpi_isr_count;
#endif
/* static unsigned int dsi_noncont_clk_period = 1; */
static bool dsi_noncont_clk_enabled = true;
static bool dsi_glitch_enable;
unsigned long g_handle_esd_flag;

static bool lcdStartTransfer;
static bool isTeSetting;
static bool dsiTeEnable;
static bool dsiTeExtEnable;

#endif

#ifdef BUILD_UBOOT
static long int get_current_time_us(void)
{
	return 0;		/* /TODO: fix me */
}
#else
static long int get_current_time_us(void)
{
	struct timeval t;

	do_gettimeofday(&t);
	return (t.tv_sec & 0xFFF) * 1000000 + t.tv_usec;
}
#endif

static void lcm_mdelay(uint32_t ms)
{
	udelay(1000 * ms);
}

void DSI_Enable_Log(bool enable)
{
	dsi_log_on = enable;
}

unsigned int try_times = 30;

static wait_queue_head_t _vsync_wait_queue;
static bool dsi_vsync;
static bool wait_dsi_vsync;
static struct hrtimer hrtimer_vsync;
#define VSYNC_US_TO_NS(x) (x * 1000)
static unsigned int vsync_timer;
static bool wait_vm_done_irq;
#if ENABLE_DSI_INTERRUPT
static irqreturn_t _DSI_InterruptHandler(int irq, void *dev_id)
{
	DSI_INT_STATUS_REG status = DSI_REG->DSI_INTSTA;
#ifdef ENABLE_DSI_ERROR_REPORT
	static unsigned int prev_error;
#endif

	MMProfileLogEx(MTKFB_MMP_Events.DSIIRQ, MMProfileFlagPulse,
		       *(unsigned int *)&status, lcdStartTransfer);
	if (dsi_log_on)
		pr_info("DSI IRQ, value = 0x%x!!\n", DISP_REG_GET(0xF400C00C));

	if (status.RD_RDY) {
		/* /write clear RD_RDY interrupt */

		/* / write clear RD_RDY interrupt must be before DSI_RACK */
		/* / because CMD_DONE will raise after DSI_RACK, */
		/* / so write clear RD_RDY after that will clear CMD_DONE too */
#ifdef ENABLE_DSI_ERROR_REPORT
		{
			unsigned int read_data[4];

			OUTREG32(&read_data[0], AS_UINT32(&DSI_REG->DSI_RX_DATA0));
			OUTREG32(&read_data[1], AS_UINT32(&DSI_REG->DSI_RX_DATA1));
			OUTREG32(&read_data[2], AS_UINT32(&DSI_REG->DSI_RX_DATA2));
			OUTREG32(&read_data[3], AS_UINT32(&DSI_REG->DSI_TRIG_STA));
			if (dsi_log_on) {
				if ((read_data[0] & 0x3) == 0x02) {
					if (read_data[0] & (~prev_error))
						pr_info
						    ("[DSI] Detect DSI error. prev:0x%08X new:0x%08X\n",
						     prev_error, read_data[0]);
				} else if ((read_data[1] & 0x3) == 0x02) {
					if (read_data[1] & (~prev_error))
						pr_info
						    ("[DSI] Detect DSI error. prev:0x%08X new:0x%08X\n",
						     prev_error, read_data[1]);
				}
			}
			MMProfileLogEx(MTKFB_MMP_Events.DSIRead, MMProfileFlagStart,
				       read_data[0], read_data[1]);
			MMProfileLogEx(MTKFB_MMP_Events.DSIRead, MMProfileFlagEnd,
				       read_data[2], read_data[3]);
		}
#endif
		do {
			/* /send read ACK */
			/* DSI_REG->DSI_RACK.DSI_RACK = 1; */
			OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);
		} while (DSI_REG->DSI_INTSTA.BUSY);

		MASKREG32(&DSI_REG->DSI_INTSTA, 0x1, 0x0);
		wake_up_interruptible(&_dsi_dcs_read_wait_queue);
		if (_dsiContext.pIntCallback)
			_dsiContext.pIntCallback(DISP_DSI_READ_RDY_INT);
	}

	if (status.CMD_DONE) {
		if (lcdStartTransfer) {
			/* The last screen update has finished. */
			if (_dsiContext.pIntCallback)
				_dsiContext.pIntCallback(DISP_DSI_CMD_DONE_INT);
#ifdef SPM_SODI_ENABLED
			spm_enable_sodi();
#endif
			DBG_OnLcdDone();
			if (dsi_glitch_enable)
				DSI_clk_HS_mode(0);

		}
		/* clear flag & wait for next trigger */
		lcdStartTransfer = false;

		/* DSI_REG->DSI_INTSTA.CMD_DONE = 0; */
		OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, CMD_DONE, 0);

		wake_up_interruptible(&_dsi_wait_queue);
		/* if(_dsiContext.pIntCallback) */
		/* _dsiContext.pIntCallback(DISP_DSI_CMD_DONE_INT); */
		/* MASKREG32(&DSI_REG->DSI_INTSTA, 0x2, 0x0); */
	}

	if (status.TE_RDY) {
		DBG_OnTeDelayDone();

		/* Write clear RD_RDY */
		/* DSI_REG->DSI_INTSTA.TE_RDY = 0; */
		OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, TE_RDY, 0);

		/* Set DSI_RACK to let DSI idle */
		/* DSI_REG->DSI_RACK.DSI_RACK = 1; */
		OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);

		wake_up_interruptible(&_dsi_wait_bta_te);


#ifndef BUILD_UBOOT
		if (wait_dsi_vsync) {
			if (EnableVSyncLog)
				pr_info("[DSI] VSync2\n");
			if (-1 != hrtimer_try_to_cancel(&hrtimer_vsync)) {
				dsi_vsync = true;
				/* hrtimer_try_to_cancel(&hrtimer_vsync); */
				if (EnableVSyncLog)
					pr_info("[DSI] VSync3\n");
				wake_up_interruptible(&_vsync_wait_queue);
			}
			/* pr_info("TE signal, and wake up\n"); */
		}
#endif
	}
	if (status.EXT_TE) {
		DBG_OnTeDelayDone();

		/* Write clear RD_RDY */
		OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, EXT_TE, 0);

		wake_up_interruptible(&_dsi_wait_ext_te);

#ifndef BUILD_UBOOT
		if (wait_dsi_vsync) {
			if (EnableVSyncLog)
				pr_info("[DSI] VSync2\n");

			if (-1 != hrtimer_try_to_cancel(&hrtimer_vsync)) {
				dsi_vsync = true;
				/* hrtimer_try_to_cancel(&hrtimer_vsync); */
				if (EnableVSyncLog)
					pr_info("[DSI] VSync3\n");

				wake_up_interruptible(&_vsync_wait_queue);
			}
			/* pr_info("TE signal, and wake up\n"); */
		}
#endif
	}


	if (status.VM_DONE) {
		OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, VM_DONE, 0);
		if (_dsiContext.pIntCallback)
			_dsiContext.pIntCallback(DISP_DSI_VMDONE_INT);
		if (dsi_log_on)
			pr_info("DSI VM done IRQ!!\n");
		/* Write clear VM_Done */
		/* DSI_REG->DSI_INTSTA.VM_DONE= 0; */
		wake_up_interruptible(&_dsi_wait_vm_done_queue);
		if (dsi_glitch_enable) {
			MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 1, 22);
			if (!mtkfb_is_suspend() && !wait_vm_done_irq) {
				if (1 == DSI_Detect_CLK_Glitch())
					pr_info("VM Done detect glitch fail!!,%d\n", __LINE__);

				/* DSI_EnableClk(); */
				DSI_Start();
			}
		}
	}

	return IRQ_HANDLED;

}
#endif
#ifndef BUILD_UBOOT
void DSI_GetVsyncCnt(void)
{
}

enum hrtimer_restart dsi_te_hrtimer_func(struct hrtimer *timer)
{
/* long long ret; */
	if (EnableVSyncLog)
		pr_info("[DSI] VSync0\n");
	if (wait_dsi_vsync) {
		dsi_vsync = true;

		if (EnableVSyncLog)
			pr_info("[DSI] VSync1\n");
		wake_up_interruptible(&_vsync_wait_queue);
	}
/* ret = hrtimer_forward_now(timer, ktime_set(0,
		VSYNC_US_TO_NS(vsync_timer))); */
/* pr_info("hrtimer callback\n"); */
	return HRTIMER_NORESTART;
}
#endif


/* static unsigned int vsync_wait_time = 0; */
void DSI_WaitTE(void)
{
#ifndef BUILD_UBOOT
	wait_dsi_vsync = true;

	hrtimer_start(&hrtimer_vsync, ktime_set(0, VSYNC_US_TO_NS(vsync_timer)), HRTIMER_MODE_REL);
	if (EnableVSyncLog)
		pr_info("[DSI] +VSync\n");
	wait_event_interruptible(_vsync_wait_queue, dsi_vsync);
	if (EnableVSyncLog)
		pr_info("[DSI] -VSync\n");
	dsi_vsync = false;
	wait_dsi_vsync = false;
#endif
}

void DSI_InitVSYNC(unsigned int vsync_interval)
{
#ifndef BUILD_UBOOT
	ktime_t ktime;

	vsync_timer = vsync_interval;
	ktime = ktime_set(0, VSYNC_US_TO_NS(vsync_timer));
	hrtimer_init(&hrtimer_vsync, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer_vsync.function = dsi_te_hrtimer_func;
/* hrtimer_start(&hrtimer_vsync, ktime, HRTIMER_MODE_REL); */
#endif
}

static bool _IsEngineBusy(void)
{
	DSI_INT_STATUS_REG status;

	status = DSI_REG->DSI_INTSTA;

	if (status.BUSY)
		return true;
	return false;
}

void wait_dsi_engine_notbusy(void)
{
	if (wait_event_timeout(_dsi_wait_vm_done_queue, !_IsEngineBusy(), HZ / 10) == 0)
		pr_info("[FB Driver] Wait disp finished timeout in early_suspend\n");
}

static void _WaitForEngineNotBusy(void)
{
	int timeOut;
#if ENABLE_DSI_INTERRUPT
	long int time;
	static const long WAIT_TIMEOUT = 2 * HZ;	/* 2 sec */
#endif

	if (DSI_REG->DSI_MODE_CTRL.MODE)
		return;

	timeOut = 200;

#if ENABLE_DSI_INTERRUPT
	time = get_current_time_us();

	if (in_interrupt()) {
		/* perform busy waiting if in interrupt context */
		while (_IsEngineBusy()) {
			msleep(20);
			if (--timeOut < 0) {

				DISP_LOG_PRINT(ANDROID_LOG_ERROR, "DSI",
					       " Wait for DSI engine not busy timeout!!!(Wait %ld us)\n",
					       get_current_time_us() - time);
				DSI_DumpRegisters();
				DSI_Reset();

				break;
			}
		}
	} else {
		while (DSI_REG->DSI_INTSTA.BUSY || DSI_REG->DSI_INTSTA.CMD_DONE) {
			long ret = wait_event_interruptible_timeout(_dsi_wait_queue,
								    !_IsEngineBusy()
								    && !(DSI_REG->DSI_INTSTA.
									 CMD_DONE),
								    WAIT_TIMEOUT);
			if (0 == ret) {
				DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
					       " Wait for DSI engine not busy timeout!!!\n");
				DSI_DumpRegisters();
				DSI_Reset();
				/* dsiTeEnable = false; */
			}
		}
	}
#else

	while (_IsEngineBusy()) {
		mdelay(10);
		/*pr_info("xuecheng, dsi wait\n"); */
		if (--timeOut < 0) {
			DISP_LOG_PRINT(ANDROID_LOG_ERROR, "DSI",
				       " Wait for DSI engine not busy timeout!!!\n");
			DSI_DumpRegisters();
			DSI_Reset();
			dsiTeEnable = false;
			dsiTeExtEnable = false;
			break;
		}
	}
	OUTREG32(&DSI_REG->DSI_INTSTA, 0x0);
#endif
}

void hdmi_dsi_waitnotbusy(void)
{
	_WaitForEngineNotBusy();
}



DSI_STATUS DSI_BackupRegisters(void)
{
	DSI_REGS *regs = &(_dsiContext.regBackup);

	/* memcpy((void*)&(_dsiContext.regBackup),
	   (void*)DDP_REG_BASE_DSI, sizeof(DSI_REGS)); */

	OUTREG32(&regs->DSI_INTEN, AS_UINT32(&DSI_REG->DSI_INTEN));
	OUTREG32(&regs->DSI_MODE_CTRL, AS_UINT32(&DSI_REG->DSI_MODE_CTRL));
	OUTREG32(&regs->DSI_TXRX_CTRL, AS_UINT32(&DSI_REG->DSI_TXRX_CTRL));
	OUTREG32(&regs->DSI_PSCTRL, AS_UINT32(&DSI_REG->DSI_PSCTRL));

	OUTREG32(&regs->DSI_VSA_NL, AS_UINT32(&DSI_REG->DSI_VSA_NL));
	OUTREG32(&regs->DSI_VBP_NL, AS_UINT32(&DSI_REG->DSI_VBP_NL));
	OUTREG32(&regs->DSI_VFP_NL, AS_UINT32(&DSI_REG->DSI_VFP_NL));
	OUTREG32(&regs->DSI_VACT_NL, AS_UINT32(&DSI_REG->DSI_VACT_NL));

	OUTREG32(&regs->DSI_HSA_WC, AS_UINT32(&DSI_REG->DSI_HSA_WC));
	OUTREG32(&regs->DSI_HBP_WC, AS_UINT32(&DSI_REG->DSI_HBP_WC));
	OUTREG32(&regs->DSI_HFP_WC, AS_UINT32(&DSI_REG->DSI_HFP_WC));
	OUTREG32(&regs->DSI_BLLP_WC, AS_UINT32(&DSI_REG->DSI_BLLP_WC));

	OUTREG32(&regs->DSI_HSTX_CKL_WC, AS_UINT32(&DSI_REG->DSI_HSTX_CKL_WC));
	OUTREG32(&regs->DSI_MEM_CONTI, AS_UINT32(&DSI_REG->DSI_MEM_CONTI));

	OUTREG32(&regs->DSI_PHY_TIMECON0, AS_UINT32(&DSI_REG->DSI_PHY_TIMECON0));
	OUTREG32(&regs->DSI_PHY_TIMECON1, AS_UINT32(&DSI_REG->DSI_PHY_TIMECON1));
	OUTREG32(&regs->DSI_PHY_TIMECON2, AS_UINT32(&DSI_REG->DSI_PHY_TIMECON2));
	OUTREG32(&regs->DSI_PHY_TIMECON3, AS_UINT32(&DSI_REG->DSI_PHY_TIMECON3));
	OUTREG32(&regs->DSI_VM_CMD_CON, AS_UINT32(&DSI_REG->DSI_VM_CMD_CON));
	return DSI_STATUS_OK;
}


DSI_STATUS DSI_RestoreRegisters(void)
{
	DSI_REGS *regs = &(_dsiContext.regBackup);

	OUTREG32(&DSI_REG->DSI_INTEN, AS_UINT32(&regs->DSI_INTEN));
	OUTREG32(&DSI_REG->DSI_MODE_CTRL, AS_UINT32(&regs->DSI_MODE_CTRL));
	OUTREG32(&DSI_REG->DSI_TXRX_CTRL, AS_UINT32(&regs->DSI_TXRX_CTRL));
	OUTREG32(&DSI_REG->DSI_PSCTRL, AS_UINT32(&regs->DSI_PSCTRL));

	OUTREG32(&DSI_REG->DSI_VSA_NL, AS_UINT32(&regs->DSI_VSA_NL));
	OUTREG32(&DSI_REG->DSI_VBP_NL, AS_UINT32(&regs->DSI_VBP_NL));
	OUTREG32(&DSI_REG->DSI_VFP_NL, AS_UINT32(&regs->DSI_VFP_NL));
	OUTREG32(&DSI_REG->DSI_VACT_NL, AS_UINT32(&regs->DSI_VACT_NL));

	OUTREG32(&DSI_REG->DSI_HSA_WC, AS_UINT32(&regs->DSI_HSA_WC));
	OUTREG32(&DSI_REG->DSI_HBP_WC, AS_UINT32(&regs->DSI_HBP_WC));
	OUTREG32(&DSI_REG->DSI_HFP_WC, AS_UINT32(&regs->DSI_HFP_WC));
	OUTREG32(&DSI_REG->DSI_BLLP_WC, AS_UINT32(&regs->DSI_BLLP_WC));

	OUTREG32(&DSI_REG->DSI_HSTX_CKL_WC, AS_UINT32(&regs->DSI_HSTX_CKL_WC));
	OUTREG32(&DSI_REG->DSI_MEM_CONTI, AS_UINT32(&regs->DSI_MEM_CONTI));

	OUTREG32(&DSI_REG->DSI_PHY_TIMECON0, AS_UINT32(&regs->DSI_PHY_TIMECON0));
	OUTREG32(&DSI_REG->DSI_PHY_TIMECON1, AS_UINT32(&regs->DSI_PHY_TIMECON1));
	OUTREG32(&DSI_REG->DSI_PHY_TIMECON2, AS_UINT32(&regs->DSI_PHY_TIMECON2));
	OUTREG32(&DSI_REG->DSI_PHY_TIMECON3, AS_UINT32(&regs->DSI_PHY_TIMECON3));
	OUTREG32(&DSI_REG->DSI_VM_CMD_CON, AS_UINT32(&regs->DSI_VM_CMD_CON));
	return DSI_STATUS_OK;
}

static void _ResetBackupedDSIRegisterValues(void)
{
	DSI_REGS *regs = &_dsiContext.regBackup;

	memset((void *)regs, 0, sizeof(DSI_REGS));
}


static void DSI_BackUpCmdQ(void)
{
	unsigned int i;
	DSI_CMDQ_REGS *regs = &(_dsiContext.cmdqBackup);

	_dsiContext.cmdq_size = AS_UINT32(&DSI_REG->DSI_CMDQ_SIZE);

	for (i = 0; i < _dsiContext.cmdq_size; i++)
		OUTREG32(&regs->data[i], AS_UINT32(&DSI_CMDQ_REG->data[i]));
}


static void DSI_RestoreCmdQ(void)
{
	unsigned int i;
	DSI_CMDQ_REGS *regs = &(_dsiContext.cmdqBackup);

	OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, AS_UINT32(&_dsiContext.cmdq_size));

	for (i = 0; i < _dsiContext.cmdq_size; i++)
		OUTREG32(&DSI_CMDQ_REG->data[i], AS_UINT32(&regs->data[i]));
}

static void _DSI_RDMA0_IRQ_Handler(unsigned int param);
spinlock_t dsi_glitch_detect_lock;
static DSI_STATUS DSI_TE_Setting(void)
{
	/* return DSI_STATUS_OK; */
	if (isTeSetting)
		return DSI_STATUS_OK;


	if (lcm_params->dsi.mode == CMD_MODE && lcm_params->dsi.lcm_ext_te_enable == true) {
		/* Enable EXT TE */
		dsiTeEnable = false;
		dsiTeExtEnable = true;
	} else {
		/* Enable BTA TE */
		dsiTeEnable = true;
		dsiTeExtEnable = false;
	}

	isTeSetting = true;

	return DSI_STATUS_OK;
}

void DSI_InitRegbase(void)
{
	DSI_REG = (PDSI_REGS) (DDP_REG_BASE_DSI);
	DSI_VM_CMD_REG = (PDSI_VM_CMDQ_REGS) (DDP_REG_BASE_DSI + 0x134);
	DSI_PHY_REG = (PDSI_PHY_REGS) (DDP_REG_BASE_MIPI);
	DSI_CMDQ_REG = (PDSI_CMDQ_REGS) (DDP_REG_BASE_DSI + 0x180);
}

DSI_STATUS DSI_Init(bool isDsiPoweredOn)
{
	DSI_STATUS ret = DSI_STATUS_OK;

	memset(&_dsiContext, 0, sizeof(_dsiContext));
	OUTREG32(DDP_REG_BASE_MMSYS_CONFIG + 0x4C, 0x0);
	if (isDsiPoweredOn)
		DSI_BackupRegisters();
	else
		_ResetBackupedDSIRegisterValues();

	ret = DSI_PowerOn();
	DSI_TE_Setting();
	OUTREG32(&DSI_REG->DSI_MEM_CONTI, DSI_WMEM_CONTI);
	ASSERT(ret == DSI_STATUS_OK);
#if ENABLE_DSI_INTERRUPT
	init_waitqueue_head(&_dsi_wait_queue);
	init_waitqueue_head(&_dsi_dcs_read_wait_queue);
	init_waitqueue_head(&_dsi_wait_bta_te);
	init_waitqueue_head(&_dsi_wait_ext_te);
	init_waitqueue_head(&_dsi_wait_vm_done_queue);
	if (request_irq(disp_dev.irq[DISP_REG_DSI], _DSI_InterruptHandler,
			IRQF_TRIGGER_LOW, "DSI", NULL) < 0) {
		DISP_LOG_PRINT(ANDROID_LOG_ERROR, "DSI", "fail to request DSI irq\n");
		return DSI_STATUS_ERROR;
	}
	/* mt65xx_irq_unmask(MT6577_DSI_IRQ_ID); */
	/* DSI_REG->DSI_INTEN.CMD_DONE=1; */
	/* DSI_REG->DSI_INTEN.RD_RDY=1; */
	/* DSI_REG->DSI_INTEN.TE_RDY = 1; */
	OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, CMD_DONE, 1);
	OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, RD_RDY, 1);
	OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, TE_RDY, 1);
	OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, EXT_TE, 1);
	OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, VM_DONE, 1);
	init_waitqueue_head(&_vsync_wait_queue);
	init_waitqueue_head(&_vsync_wait_queue);
#endif
	if (lcm_params->dsi.mode == (LCM_DSI_MODE_CON) DSI_CMD_MODE)
		disp_register_irq(DISP_MODULE_RDMA0, _DSI_RDMA0_IRQ_Handler);
	spin_lock_init(&dsi_glitch_detect_lock);
	return DSI_STATUS_OK;
}


DSI_STATUS DSI_Deinit(void)
{
	DSI_STATUS ret = DSI_PowerOff();

	ASSERT(ret == DSI_STATUS_OK);

	return DSI_STATUS_OK;
}

DSI_STATUS DSI_PowerOn(void)
{
	if (!s_isDsiPowerOn) {
#ifdef DDP_USE_CLOCK_API
		int ret = 0;

		clk_prepare_enable(disp_dev.clk_map[DISP_REG_DSI][0]);
		clk_prepare_enable(disp_dev.clk_map[DISP_REG_DSI][1]);
		clk_prepare_enable(disp_dev.clk_map[DISP_REG_UFOE][0]);
		if (ret > 0) {
			DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
				       "DSI power manager API return false\n");
		}
#endif
		s_isDsiPowerOn = true;
	}
	return DSI_STATUS_OK;
}


DSI_STATUS DSI_PowerOff(void)
{
	if (s_isDsiPowerOn) {
#ifdef DDP_USE_CLOCK_API
		int ret = 0;

		clk_disable_unprepare(disp_dev.clk_map[DISP_REG_DSI][0]);
		clk_disable_unprepare(disp_dev.clk_map[DISP_REG_DSI][1]);
		clk_disable_unprepare(disp_dev.clk_map[DISP_REG_UFOE][0]);
		if (ret > 0) {
			DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
				       "DSI power manager API return false\n");
		}
#endif
		s_isDsiPowerOn = false;
	}
	return DSI_STATUS_OK;
}

DSI_STATUS DSI_WaitForNotBusy(void)
{
	_WaitForEngineNotBusy();

	return DSI_STATUS_OK;
}


static void DSI_WaitBtaTE(void)
{
	DSI_T0_INS t0;
#if ENABLE_DSI_INTERRUPT
	long ret;
	static const long WAIT_TIMEOUT = 2 * HZ;	/* 2 sec */
#else
	long int dsi_current_time;
#endif

	if (DSI_REG->DSI_MODE_CTRL.MODE != CMD_MODE)
		return;

	_WaitForEngineNotBusy();

	DSI_clk_HS_mode(0);
	/* backup command queue setting. */
	DSI_BackUpCmdQ();

	t0.CONFG = 0x20;	/* /TE */
	t0.Data0 = 0;
	t0.Data_ID = 0;
	t0.Data1 = 0;

	OUTREG32(&DSI_CMDQ_REG->data[0], AS_UINT32(&t0));
	OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);

	/* DSI_REG->DSI_START.DSI_START=0; */
	/* DSI_REG->DSI_START.DSI_START=1; */
	OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 0);
	OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 1);

	/* wait BTA TE command complete. */
	_WaitForEngineNotBusy();

	/* restore command queue setting. */
	DSI_RestoreCmdQ();

#if ENABLE_DSI_INTERRUPT

	ret = wait_event_interruptible_timeout(_dsi_wait_bta_te, !_IsEngineBusy(), WAIT_TIMEOUT);

	if (0 == ret) {
		DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
			       "Wait for _dsi_wait_bta_te(DSI_INTSTA.TE_RDY) ready timeout!!!\n");

		/* Set DSI_RACK to let DSI idle */
		/* DSI_REG->DSI_RACK.DSI_RACK = 1; */
		OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);

		DSI_DumpRegisters();
		/* /do necessary reset here */
		DSI_Reset();
		dsiTeEnable = false;	/* disable TE */
		return;
	}
	/* After setting DSI_RACK, it needs to wait for CMD_DONE interrupt. */
	_WaitForEngineNotBusy();

#else

	dsi_current_time = get_current_time_us();

	while (DSI_REG->DSI_INTSTA.TE_RDY == 0) {
		if (get_current_time_us() - dsi_current_time > 100 * 1000) {
			DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI", "Wait for TE_RDY timeout!!!\n");

			/* Set DSI_RACK to let DSI idle */
			/* DSI_REG->DSI_RACK.DSI_RACK = 1; */
			OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);

			DSI_DumpRegisters();

			/* do necessary reset here */
			DSI_Reset();
			dsiTeEnable = false;	/* disable TE */
			break;
		}
	}

	/* Write clear RD_RDY */
	/* DSI_REG->DSI_INTSTA.TE_RDY = 0; */
	OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, TE_RDY, 0);

	/* Set DSI_RACK to let DSI idle */
	/* DSI_REG->DSI_RACK.DSI_RACK = 1; */
	OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);
	if (!dsiTeEnable) {
		DSI_LP_Reset();
		return;
	}
	dsi_current_time = get_current_time_us();

	while (DSI_REG->DSI_INTSTA.CMD_DONE == 0) {
		if (get_current_time_us() - dsi_current_time > 100 * 1000) {
			DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI", "Wait for CMD_DONE timeout!!!\n");

			/* Set DSI_RACK to let DSI idle */
			/* DSI_REG->DSI_RACK.DSI_RACK = 1; */
			OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);

			DSI_DumpRegisters();

			/* /do necessary reset here */
			DSI_Reset();
			dsiTeEnable = false;	/* disable TE */
			break;
		}
	}

	/* Write clear CMD_DONE */
	/* DSI_REG->DSI_INTSTA.CMD_DONE = 0; */
	OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, CMD_DONE, 0);

#endif
	DSI_LP_Reset();
}

static void DSI_WaitExternalTE(void)
{
#if ENABLE_DSI_INTERRUPT
	long ret;
	static const long WAIT_TIMEOUT = 2 * HZ;	/* 2 sec */
#else
	long int dsi_current_time;
#endif


	if (DSI_REG->DSI_MODE_CTRL.MODE != CMD_MODE)
		return;


	OUTREGBIT(DSI_TXRX_CTRL_REG, DSI_REG->DSI_TXRX_CTRL, EXT_TE_EN, 1);
	OUTREGBIT(DSI_TXRX_CTRL_REG, DSI_REG->DSI_TXRX_CTRL, EXT_TE_EDGE, 0);

#if ENABLE_DSI_INTERRUPT
	ret = wait_event_interruptible_timeout(_dsi_wait_ext_te, !_IsEngineBusy(), WAIT_TIMEOUT);

	if (0 == ret) {
		DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
			       "Wait for _dsi_wait_ext_te(DSI_INTSTA.EXT_TE) ready timeout!!!\n");

		OUTREGBIT(DSI_TXRX_CTRL_REG, DSI_REG->DSI_TXRX_CTRL, EXT_TE_EN, 0);
		DSI_DumpRegisters();
		/* /do necessary reset here */
		DSI_Reset();
		dsiTeExtEnable = false;	/* disable TE */

		return;
	}
#else
	dsi_current_time = get_current_time_us();

	while (DSI_REG->DSI_INTSTA.EXT_TE == 0) {
		if (get_current_time_us() - dsi_current_time > 100 * 1000) {
			DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI", "Wait for EXT_TE timeout!!!\n");

			DSI_DumpRegisters();

			/* do necessary reset here */
			DSI_Reset();
			dsiTeExtEnable = false;	/* disable TE */

			break;
		}
	}

	/* Write clear EXT_TE */
	OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, EXT_TE, 0);

	if (!dsiTeExtEnable) {
		DSI_LP_Reset();
		return;
	}
#endif

	DSI_LP_Reset();
}

DSI_STATUS DSI_EnableClk(void)
{
	/* _WaitForEngineNotBusy(); */

	/* DSI_REG->DSI_START.DSI_START=0; */
	OUTREGBIT(DSI_COM_CTRL_REG, DSI_REG->DSI_COM_CTRL, DSI_EN, 1);

	return DSI_STATUS_OK;
}

DSI_STATUS DSI_Start(void)
{
	OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 0);
	OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 1);
	return DSI_STATUS_OK;
}

DSI_STATUS DSI_EnableVM_CMD(void)
{
	OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, VM_CMD_START, 0);
	OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, VM_CMD_START, 1);
	return DSI_STATUS_OK;
}

DSI_STATUS DSI_StartTransfer(bool isMutexLocked)
{
	/* needStartDSI = 1: For command mode or the first time of video mode. */
	/* After the first time of video mode.
	   Configuration is applied in ConfigurationUpdateTask. */

#ifdef SPM_SODI_ENABLED
	if (DSI_REG->DSI_MODE_CTRL.MODE == CMD_MODE)
		spm_disable_sodi();
#endif
	if (!isMutexLocked) {
		disp_path_get_mutex();
		mutex_lock(&OverlaySettingMutex);
		LCD_ConfigOVL();
	}
	/* Insert log for trigger point. */
	DBG_OnTriggerLcd();

	if (dsiTeEnable)
		DSI_WaitBtaTE();
	if (dsiTeExtEnable)
		DSI_WaitExternalTE();


	if (dsi_glitch_enable) {
		spin_lock_irq(&dsi_glitch_detect_lock);
		if (1 == DSI_Detect_CLK_Glitch()) {
			if (!force_transfer) {
				spin_unlock_irq(&dsi_glitch_detect_lock);
				if (!isMutexLocked) {
					mutex_unlock(&OverlaySettingMutex);
					disp_path_release_mutex();
				}
				if (_dsiContext.pIntCallback)
					_dsiContext.pIntCallback(DISP_DSI_CMD_DONE_INT);
				return DSI_STATUS_OK;
			}
		}
		spin_unlock_irq(&dsi_glitch_detect_lock);
	}
	_WaitForEngineNotBusy();
	lcdStartTransfer = true;
	/* To trigger frame update. */
	DSI_clk_HS_mode(1);
	DSI_Start();
	if (!isMutexLocked) {
		mutex_unlock(&OverlaySettingMutex);
		disp_path_release_mutex();
	}
	return DSI_STATUS_OK;
}

unsigned int glitch_detect_fail_cnt = 0;

static unsigned int DSI_Detect_CLK_Glitch_Default(void)
{
	DSI_T0_INS t0;
	char i;
	int read_timeout_cnt = 10000;
	int read_timeout_ret = 0;
	unsigned long long start_time, end_time;
	DSI_RX_DATA_REG read_data0;
	DSI_RX_DATA_REG read_data1;

	if (glitch_detect_fail_cnt > 2)
		return 0;


	while (DSI_REG->DSI_INTSTA.BUSY)
		;
	OUTREG32(&DSI_REG->DSI_INTSTA, 0x0);

	DSI_BackUpCmdQ();
	DSI_SetMode(CMD_MODE);
	OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, RD_RDY, 0);
	OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, CMD_DONE, 0);

	OUTREG32(&DSI_CMDQ_REG->data[0], 0x00340500);	/* turn off TE */
	OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);

	OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 0);
	OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 1);
	while (DSI_REG->DSI_INTSTA.CMD_DONE == 0)
		;
	OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, CMD_DONE, 0);

	MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 0);
#if 1
	OUTREG32(&DSI_CMDQ_REG->data[0], 0x00ff1500);
	OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);

	OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 0);
	OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 1);
	while (DSI_REG->DSI_INTSTA.CMD_DONE == 0)
		;
	OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, CMD_DONE, 0);
#endif
	for (i = 0; i < try_times; i++) {
		DSI_clk_HS_mode(0);

		MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 9);
		while ((DISP_REG_GET(&DSI_REG->DSI_STATE_DBG0) & 0x1) == 0)
			;

		OUTREGBIT(DSI_COM_CTRL_REG, DSI_REG->DSI_COM_CTRL, DSI_RESET, 0);
		OUTREGBIT(DSI_COM_CTRL_REG, DSI_REG->DSI_COM_CTRL, DSI_RESET, 1);
		OUTREGBIT(DSI_COM_CTRL_REG, DSI_REG->DSI_COM_CTRL, DSI_RESET, 0);

		MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 10);
		if (i > 0)
			MASKREG32(DDP_REG_BASE_MIPI + 0x04, 0x20, 0x0);

		DSI_clk_HS_mode(1);
		MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 1);
		while ((DISP_REG_GET(&DSI_REG->DSI_STATE_DBG0) & 0x40000) == 0)
			;
		MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 2);
		if (i > 0)
			MASKREG32(DDP_REG_BASE_MIPI + 0x04, 0x20, 0x20);

		/* OUTREG32(&DSI_CMDQ_REG->data[0], 0x00290508); */
		OUTREG32(&DSI_CMDQ_REG->data[0], 0x00351508);
		OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);

		OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 0);
		OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 1);
		read_timeout_cnt = 1000000;
		MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 3);
		start_time = sched_clock();
		while (DSI_REG->DSI_INTSTA.BUSY) {
			end_time = sched_clock();
			if (((unsigned int)sched_clock() - (unsigned int)start_time) > 50000) {
				DISP_LOG_PRINT(ANDROID_LOG_ERROR, "DSI",
					       " Wait for DSI engine not busy timeout!!!:%d\n",
					       __LINE__);
				DSI_Reset();
				break;
			}
		}
		OUTREG32(&DSI_REG->DSI_INTSTA, 0x0);
/* spin_unlock_irq(&dsi_glitch_detect_lock); */
		MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 4);

		t0.CONFG = 0x04;
		t0.Data0 = 0;
		t0.Data_ID = 0;
		t0.Data1 = 0;

		OUTREG32(&DSI_CMDQ_REG->data[0], AS_UINT32(&t0));
		OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);

		OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 0);
		OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 1);



		read_timeout_cnt = 1000;
		MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 5);
		start_time = sched_clock();
		while (DSI_REG->DSI_INTSTA.RD_RDY == 0) {
			end_time = sched_clock();
			if (((unsigned int)sched_clock() - (unsigned int)start_time) > 50000) {
				if (glitch_log_on)
					pr_info("Test log 4:Polling DSI read ready timeout,%d us\n",
						(unsigned int)sched_clock() -
						(unsigned int)start_time);

				MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 13);
#if 1
				OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);
				DSI_Reset();
#endif
				read_timeout_ret = 1;
				break;
			}
		}
		if (1 == read_timeout_ret) {
			read_timeout_ret = 0;
/* return 1; */
			continue;
		}
		MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 6);
		OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);
		OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, RD_RDY, 0);

		if (((DSI_REG->DSI_TRIG_STA.TRIG2)) == 1) {
			break;
/* continue; */
		} else {
			/* read error report */
			OUTREG32(&read_data0, AS_UINT32(&DSI_REG->DSI_RX_DATA0));
			OUTREG32(&read_data1, AS_UINT32(&DSI_REG->DSI_RX_DATA1));
			if (glitch_log_on) {
				pr_info("read_data0, %x,%x,%x,%x\n", read_data0.byte0,
					read_data0.byte1, read_data0.byte2, read_data0.byte3);
				pr_info("read_data1, %x,%x,%x,%x\n", read_data1.byte0,
					read_data1.byte1, read_data1.byte2, read_data1.byte3);
			}
			if (((read_data0.byte1 & 0x7) != 0) || ((read_data0.byte2 & 0x3) != 0))
				continue;
			else
				break;	/* jump out the for loop ,go to refresh */
		}
	}
#if 1
	if (i > 0)
		pr_info("detect times:%d\n", i);
#endif

	MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 7);
#if 1
	switch (lcm_params->dsi.LANE_NUM) {
	case LCM_FOUR_LANE:
		OUTREG32(DDP_REG_BASE_MIPI + 0x84, 0x3CF3C7B1);
		break;
	case LCM_THREE_LANE:
		OUTREG32(DDP_REG_BASE_MIPI + 0x84, 0x00F3C7B1);
		break;
	default:
		OUTREG32(DDP_REG_BASE_MIPI + 0x84, 0x0003C7B1);
	}

	OUTREG32(DDP_REG_BASE_MIPI + 0x88, 0x0);
	OUTREG32(DDP_REG_BASE_MIPI + 0x80, 0x1);

	DSI_REG->DSI_COM_CTRL.DSI_RESET = 0;
	DSI_REG->DSI_COM_CTRL.DSI_RESET = 1;
	DSI_REG->DSI_COM_CTRL.DSI_RESET = 0;

	DSI_clk_HS_mode(1);

	while ((DISP_REG_GET(&DSI_REG->DSI_STATE_DBG0) & 0x40000) == 0)
		;

	OUTREG32(DDP_REG_BASE_MIPI + 0x80, 0x0);
#endif
	start_time = sched_clock();
	while (DSI_REG->DSI_INTSTA.BUSY) {
		end_time = sched_clock();
		if (((unsigned int)sched_clock() - (unsigned int)start_time) > 50000) {
			DSI_Reset();
			break;
		}
	}
	OUTREG32(&DSI_REG->DSI_INTSTA, 0x0);

	OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, RD_RDY, 1);
	OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, CMD_DONE, 1);
	DSI_RestoreCmdQ();
	DSI_SetMode(lcm_params->dsi.mode);
	MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 8);
#if 1
/* if(glitch_log_on) */
	if (i == try_times) {
		glitch_detect_fail_cnt++;
		return 1;
	}
#endif
	glitch_detect_fail_cnt = 0;
	return 0;
}

static unsigned int DSI_Detect_CLK_Glitch_Parallel(void)
{
	DSI_T0_INS t0;
	char i;
	int read_timeout_cnt = 10000;
	int read_timeout_ret = 0;
	int read_IC_ID = 0;
	unsigned long long start_time, end_time;
	DSI_RX_DATA_REG read_data0;
	DSI_RX_DATA_REG read_data1;

	if (glitch_detect_fail_cnt > 2)
		return 0;


	while (DSI_REG->DSI_INTSTA.BUSY)
		;
	OUTREG32(&DSI_REG->DSI_INTSTA, 0x0);

	DSI_BackUpCmdQ();
	DSI_SetMode(CMD_MODE);
	OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, RD_RDY, 0);
	OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, CMD_DONE, 0);
	MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 0);
	for (i = 0; i < try_times * 4; i++) {
		if (read_IC_ID == 0) {

			DSI_clk_HS_mode(0);

			MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 9);
			while ((DISP_REG_GET(&DSI_REG->DSI_STATE_DBG0) & 0x1) == 0)
				;

			OUTREGBIT(DSI_COM_CTRL_REG, DSI_REG->DSI_COM_CTRL, DSI_RESET, 0);
			OUTREGBIT(DSI_COM_CTRL_REG, DSI_REG->DSI_COM_CTRL, DSI_RESET, 1);	/* reset */
			OUTREGBIT(DSI_COM_CTRL_REG, DSI_REG->DSI_COM_CTRL, DSI_RESET, 0);

			MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 10);
			if (i > 0)
				MASKREG32(DDP_REG_BASE_MIPI + 0x04, 0x20, 0x0);

			DSI_clk_HS_mode(1);

			MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 1);
			while ((DISP_REG_GET(&DSI_REG->DSI_STATE_DBG0) & 0x40000) == 0)
				;
			MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 2);
			if (i > 0)
				MASKREG32(DDP_REG_BASE_MIPI + 0x04, 0x20, 0x20);

			/* OUTREG32(&DSI_CMDQ_REG->data[0], 0x00290508); */

		}
#if 1				/* HS command */
		OUTREG32(&DSI_CMDQ_REG->data[0], 0xAA801508);
		OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);

		OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 0);
		OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 1);

		read_timeout_cnt = 1000000;
		MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 3);
		start_time = sched_clock();
		while (DSI_REG->DSI_INTSTA.BUSY) {
			end_time = sched_clock();
			if (((unsigned int)sched_clock() - (unsigned int)start_time) > 50000) {
				DISP_LOG_PRINT(ANDROID_LOG_ERROR, "DSI",
					       " Wait for DSI engine not busy timeout!!!:%d\n",
					       __LINE__);
				DSI_Reset();
				break;
			}
		}
		OUTREG32(&DSI_REG->DSI_INTSTA, 0x0);
		/* spin_unlock_irq(&dsi_glitch_detect_lock); */
		MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 4);

#endif
		/* LP command */
		if (read_IC_ID == 0) {
			/* OUTREG32(&DSI_CMDQ_REG->data[0], 0x00023902); */
			/* OUTREG32(&DSI_CMDQ_REG->data[1], 0x000010B5); */
			OUTREG32(&DSI_CMDQ_REG->data[0], 0x10B51500);
		} else {
			/* OUTREG32(&DSI_CMDQ_REG->data[0], 0x00023902); */
			/* OUTREG32(&DSI_CMDQ_REG->data[1], 0x000090B5); */
			OUTREG32(&DSI_CMDQ_REG->data[0], 0x90B51500);
		}

		OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);
		OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 0);
		OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 1);
		while (DSI_REG->DSI_INTSTA.CMD_DONE == 0)
			;
		OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, CMD_DONE, 0);

		t0.CONFG = 0x04;
		t0.Data0 = 0;
		t0.Data_ID = 0;
		t0.Data1 = 0;

		OUTREG32(&DSI_CMDQ_REG->data[0], AS_UINT32(&t0));
		OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);

		OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 0);
		OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 1);


		read_timeout_cnt = 1000;
		MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 5);
		start_time = sched_clock();
		while (DSI_REG->DSI_INTSTA.RD_RDY == 0) {
			end_time = sched_clock();
			if (((unsigned int)sched_clock() - (unsigned int)start_time) > 50000) {
				if (glitch_log_on)
					pr_info("Test log 4:Polling DSI read ready timeout,%d us\n",
						(unsigned int)sched_clock() -
						(unsigned int)start_time);

				MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 13);
#if 1
				OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);
				DSI_Reset();
#endif
				read_timeout_ret = 1;
				break;
			}
		}
		if (1 == read_timeout_ret) {
			read_timeout_ret = 0;
			pr_info("iii detect timeout ID:%d\n", read_IC_ID);
			read_IC_ID = 0;
			continue;
		}
		MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 6);
		OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);
		OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, RD_RDY, 0);

		if (((DSI_REG->DSI_TRIG_STA.TRIG2)) == 1) {
			if (read_IC_ID == 0) {
				read_IC_ID = 1;
				continue;
			}
			break;
		}

		/* read error report */
		OUTREG32(&read_data0, AS_UINT32(&DSI_REG->DSI_RX_DATA0));
		OUTREG32(&read_data1, AS_UINT32(&DSI_REG->DSI_RX_DATA1));
		if (glitch_log_on) {
			pr_info("read_data0, %x,%x,%x,%x\n", read_data0.byte0,
				read_data0.byte1, read_data0.byte2, read_data0.byte3);
			pr_info("read_data1, %x,%x,%x,%x\n", read_data1.byte0,
				read_data1.byte1, read_data1.byte2, read_data1.byte3);

			if (((read_data0.byte1 & 0x4) != 0)
			    || ((read_data0.byte2 & 0x3) != 0)) {
				pr_info("111 ID:%d ECC err read_data0, %x,%x,%x,%x\n",
					read_IC_ID, read_data0.byte0, read_data0.byte1,
					read_data0.byte2, read_data0.byte3);
			}
		}
		if (((read_data0.byte1 & 0x7) != 0) || ((read_data0.byte2 & 0x3) != 0)) {
			pr_info("read_data0, %x,%x,%x,%x\n", read_data0.byte0,
				read_data0.byte1, read_data0.byte2, read_data0.byte3);
			pr_info("iii detect error ID:%d\n", read_IC_ID);
			read_IC_ID = 0;
			continue;
		} else {
			if (read_IC_ID == 0) {
				read_IC_ID = 1;
				continue;
			}
			break;	/* jump out the for loop ,go to refresh */
		}

	}
#if 1
	if (i > 1)
		pr_info("detect times:%d\n", i);
#endif

	MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 7);
#if 1
	switch (lcm_params->dsi.LANE_NUM) {
	case LCM_FOUR_LANE:
		OUTREG32(DDP_REG_BASE_MIPI + 0x84, 0x3CF3C7B1);
		break;
	case LCM_THREE_LANE:
		OUTREG32(DDP_REG_BASE_MIPI + 0x84, 0x00F3C7B1);
		break;
	default:
		OUTREG32(DDP_REG_BASE_MIPI + 0x84, 0x0003C7B1);
	}

	OUTREG32(DDP_REG_BASE_MIPI + 0x88, 0x0);
	OUTREG32(DDP_REG_BASE_MIPI + 0x80, 0x1);

	DSI_REG->DSI_COM_CTRL.DSI_RESET = 0;
	DSI_REG->DSI_COM_CTRL.DSI_RESET = 1;
	DSI_REG->DSI_COM_CTRL.DSI_RESET = 0;

	DSI_clk_HS_mode(1);

	while ((DISP_REG_GET(&DSI_REG->DSI_STATE_DBG0) & 0x40000) == 0)
		;

	OUTREG32(DDP_REG_BASE_MIPI + 0x80, 0x0);
#endif
	start_time = sched_clock();
	while (DSI_REG->DSI_INTSTA.BUSY) {
		end_time = sched_clock();
		if (((unsigned int)sched_clock() - (unsigned int)start_time) > 50000) {
			DSI_Reset();
			break;
		}
	}
	OUTREG32(&DSI_REG->DSI_INTSTA, 0x0);

	OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, RD_RDY, 1);
	OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, CMD_DONE, 1);
	DSI_RestoreCmdQ();
	DSI_SetMode(lcm_params->dsi.mode);
	MMProfileLogEx(MTKFB_MMP_Events.Debug, MMProfileFlagPulse, 0, 8);
#if 1
	/* if(glitch_log_on) */
	if (i == try_times * 4) {
		glitch_detect_fail_cnt++;
		return 1;
	}
#endif
	glitch_detect_fail_cnt = 0;
	return 0;
}

unsigned int DSI_Detect_CLK_Glitch(void)
{
	if (lcm_params->dsi.compatibility_for_nvk == 1)
		return DSI_Detect_CLK_Glitch_Default();
	else if (lcm_params->dsi.compatibility_for_nvk == 2)
		return DSI_Detect_CLK_Glitch_Parallel();
	else
		return DSI_Detect_CLK_Glitch_Default();

}


DSI_STATUS DSI_Config_VDO_FRM_Mode(void)
{
	try_times = 30;
	force_transfer = true;
	OUTREGBIT(DSI_MODE_CTRL_REG, DSI_REG->DSI_MODE_CTRL, FRM_MODE, 1);
	return DSI_STATUS_OK;
}

DSI_STATUS DSI_DisableClk(void)
{
	/* DSI_REG->DSI_START.DSI_START=0; */
	OUTREGBIT(DSI_COM_CTRL_REG, DSI_REG->DSI_COM_CTRL, DSI_EN, 0);

	return DSI_STATUS_OK;
}


DSI_STATUS DSI_Reset(void)
{
	/* DSI_REG->DSI_COM_CTRL.DSI_RESET = 1; */
	OUTREGBIT(DSI_COM_CTRL_REG, DSI_REG->DSI_COM_CTRL, DSI_RESET, 1);
	/* lcm_mdelay(5); */
	/* DSI_REG->DSI_COM_CTRL.DSI_RESET = 0; */
	OUTREGBIT(DSI_COM_CTRL_REG, DSI_REG->DSI_COM_CTRL, DSI_RESET, 0);


	return DSI_STATUS_OK;
}

DSI_STATUS DSI_LP_Reset(void)
{
#if 0
	_WaitForEngineNotBusy();
	OUTREGBIT(DSI_COM_CTRL_REG, DSI_REG->DSI_COM_CTRL, DSI_RESET, 1);
	OUTREGBIT(DSI_COM_CTRL_REG, DSI_REG->DSI_COM_CTRL, DSI_RESET, 0);
#endif
	return DSI_STATUS_OK;
}

DSI_STATUS DSI_SetMode(unsigned int mode)
{

	/* DSI_REG->DSI_MODE_CTRL.MODE = mode; */
	OUTREGBIT(DSI_MODE_CTRL_REG, DSI_REG->DSI_MODE_CTRL, MODE, mode);
	return DSI_STATUS_OK;
}

static void _DSI_RDMA0_IRQ_Handler(unsigned int param)
{
	if (_dsiContext.pIntCallback) {
		if (param & 4) {
			MMProfileLogEx(MTKFB_MMP_Events.ScreenUpdate, MMProfileFlagEnd, param, 0);
			_dsiContext.pIntCallback(DISP_DSI_SCREEN_UPDATE_END_INT);
		}
		if (param & 8)
			MMProfileLogEx(MTKFB_MMP_Events.ScreenUpdate, MMProfileFlagEnd, param, 0);

		if (param & 2) {
			MMProfileLogEx(MTKFB_MMP_Events.ScreenUpdate, MMProfileFlagStart, param, 0);
			_dsiContext.pIntCallback(DISP_DSI_SCREEN_UPDATE_START_INT);
		}
		if (param & 0x20) {
			_dsiContext.pIntCallback(DISP_DSI_TARGET_LINE_INT);
			_dsiContext.pIntCallback(DISP_DSI_VSYNC_INT);
		}
	}
}

static void _DSI_MUTEX_IRQ_Handler(unsigned int param)
{
	if (_dsiContext.pIntCallback) {
#ifndef CONFIG_MTK_OVERLAY_ENGINE_SUPPORT
		if (param & 1)
#endif
			_dsiContext.pIntCallback(DISP_DSI_REG_UPDATE_INT);
	}
}

DSI_STATUS DSI_SleepOut(void)
{
	OUTREGBIT(DSI_MODE_CTRL_REG, DSI_REG->DSI_MODE_CTRL, SLEEP_MODE, 1);
	/* cycle to 1ms for 520MHz */
	OUTREGBIT(DSI_PHY_TIMCON4_REG, DSI_REG->DSI_PHY_TIMECON4, ULPS_WAKEUP, 0x22E09);

	return DSI_STATUS_OK;
}


DSI_STATUS DSI_Wakeup(void)
{
	OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, SLEEPOUT_START, 0);
	OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, SLEEPOUT_START, 1);
	mdelay(1);

	OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, SLEEPOUT_START, 0);
	OUTREGBIT(DSI_MODE_CTRL_REG, DSI_REG->DSI_MODE_CTRL, SLEEP_MODE, 0);

	return DSI_STATUS_OK;
}

DSI_STATUS DSI_EnableInterrupt(DISP_INTERRUPT_EVENTS eventID)
{
#if ENABLE_DSI_INTERRUPT
	switch (eventID) {
	case DISP_DSI_READ_RDY_INT:
		/* DSI_REG->DSI_INTEN.RD_RDY = 1; */
		OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, RD_RDY, 1);
		break;
	case DISP_DSI_CMD_DONE_INT:
		/* DSI_REG->DSI_INTEN.CMD_DONE = 1; */
		OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, CMD_DONE, 1);
		break;
	case DISP_DSI_VMDONE_INT:
		OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, VM_DONE, 1);
		break;
	case DISP_DSI_VSYNC_INT:
		disp_register_irq(DISP_MODULE_RDMA0, _DSI_RDMA0_IRQ_Handler);
		break;
	case DISP_DSI_TARGET_LINE_INT:
		disp_register_irq(DISP_MODULE_RDMA0, _DSI_RDMA0_IRQ_Handler);
		break;
	case DISP_DSI_SCREEN_UPDATE_START_INT:
		disp_register_irq(DISP_MODULE_RDMA0, _DSI_RDMA0_IRQ_Handler);
		break;
	case DISP_DSI_SCREEN_UPDATE_END_INT:
		disp_register_irq(DISP_MODULE_RDMA0, _DSI_RDMA0_IRQ_Handler);
		break;
	case DISP_DSI_REG_UPDATE_INT:
		/* wake_up_interruptible(&_dsi_reg_update_wq); */
		disp_register_irq(DISP_MODULE_MUTEX, _DSI_MUTEX_IRQ_Handler);
		break;
	default:
		return DSI_STATUS_ERROR;
	}

	return DSI_STATUS_OK;
#else
	/* /TODO: warning log here */
	return DSI_STATUS_OK;
#endif
}


DSI_STATUS DSI_SetInterruptCallback(void (*pCB) (DISP_INTERRUPT_EVENTS))
{
	_dsiContext.pIntCallback = pCB;

	return DSI_STATUS_OK;
}

DSI_STATUS DSI_handle_TE(void)
{

	unsigned int data_array;

	/* data_array=0x00351504; */
	/* DSI_set_cmdq(&data_array, 1, 1); */

	/* lcm_mdelay(10); */

	/* RACT */
	/* data_array=1; */
	/* OUTREG32(&DSI_REG->DSI_RACK, data_array); */

	/* TE + BTA */
	data_array = 0x24;
	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "[DISP] DSI_handle_TE TE + BTA !!\n");
	OUTREG32(&DSI_CMDQ_REG->data, data_array);

	/* DSI_CMDQ_REG->data.byte0=0x24; */
	/* DSI_CMDQ_REG->data.byte1=0; */
	/* DSI_CMDQ_REG->data.byte2=0; */
	/* DSI_CMDQ_REG->data.byte3=0; */

	/* DSI_REG->DSI_CMDQ_SIZE.CMDQ_SIZE=1; */
	OUTREGBIT(DSI_CMDQ_CTRL_REG, DSI_REG->DSI_CMDQ_SIZE, CMDQ_SIZE, 1);

	/* DSI_REG->DSI_START.DSI_START=0; */
	/* DSI_REG->DSI_START.DSI_START=1; */
	OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 0);
	OUTREGBIT(DSI_START_REG, DSI_REG->DSI_START, DSI_START, 1);


	/* wait TE Trigger status */
/* do */
/* { */
	lcm_mdelay(10);

	data_array = DISP_REG_GET(&DSI_REG->DSI_INTSTA);
	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "[DISP] DSI INT state : %x !!\n", data_array);

	data_array = DISP_REG_GET(&DSI_REG->DSI_TRIG_STA);
	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "[DISP] DSI TRIG TE status check : %x !!\n",
		       data_array);
/* } while(!(data_array&0x4)); */

	/* RACT */
	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "[DISP] DSI Set RACT !!\n");
	data_array = 1;
	OUTREG32(&DSI_REG->DSI_RACK, data_array);

	return DSI_STATUS_OK;

}





void DSI_PHY_clk_setting(LCM_PARAMS *lcm_params)
{
	unsigned int data_Rate = lcm_params->dsi.PLL_CLOCK * 2;
	unsigned int txdiv, pcw;
/* unsigned int fmod = 30;//Fmod = 30KHz by default */
	unsigned int delta1 = 5;	/* Delta1 is SSC range, default is 0%~-5% */
	unsigned int pdelta1;

	OUTREGBIT(MIPITX_DSI_TOP_CON_REG, DSI_PHY_REG->MIPITX_DSI_TOP_CON, RG_DSI_LNT_IMP_CAL_CODE,
		  8);
	OUTREGBIT(MIPITX_DSI_TOP_CON_REG, DSI_PHY_REG->MIPITX_DSI_TOP_CON, RG_DSI_LNT_HS_BIAS_EN,
		  1);

	OUTREGBIT(MIPITX_DSI_BG_CON_REG, DSI_PHY_REG->MIPITX_DSI_BG_CON, RG_DSI_V032_SEL, 4);
	OUTREGBIT(MIPITX_DSI_BG_CON_REG, DSI_PHY_REG->MIPITX_DSI_BG_CON, RG_DSI_V04_SEL, 4);
	OUTREGBIT(MIPITX_DSI_BG_CON_REG, DSI_PHY_REG->MIPITX_DSI_BG_CON, RG_DSI_V072_SEL, 4);
	OUTREGBIT(MIPITX_DSI_BG_CON_REG, DSI_PHY_REG->MIPITX_DSI_BG_CON, RG_DSI_V10_SEL, 4);
	OUTREGBIT(MIPITX_DSI_BG_CON_REG, DSI_PHY_REG->MIPITX_DSI_BG_CON, RG_DSI_V12_SEL, 4);
	OUTREGBIT(MIPITX_DSI_BG_CON_REG, DSI_PHY_REG->MIPITX_DSI_BG_CON, RG_DSI_BG_CKEN, 1);
	OUTREGBIT(MIPITX_DSI_BG_CON_REG, DSI_PHY_REG->MIPITX_DSI_BG_CON, RG_DSI_BG_CORE_EN, 1);
	mdelay(10);

	OUTREGBIT(MIPITX_DSI0_CON_REG, DSI_PHY_REG->MIPITX_DSI0_CON, RG_DSI0_CKG_LDOOUT_EN, 1);
	OUTREGBIT(MIPITX_DSI0_CON_REG, DSI_PHY_REG->MIPITX_DSI0_CON, RG_DSI0_LDOCORE_EN, 1);

	OUTREGBIT(MIPITX_DSI_PLL_PWR_REG, DSI_PHY_REG->MIPITX_DSI_PLL_PWR, DA_DSI0_MPPLL_SDM_PWR_ON,
		  1);
	OUTREGBIT(MIPITX_DSI_PLL_PWR_REG, DSI_PHY_REG->MIPITX_DSI_PLL_PWR, DA_DSI0_MPPLL_SDM_ISO_EN,
		  1);
	mdelay(10);

	OUTREGBIT(MIPITX_DSI_PLL_PWR_REG, DSI_PHY_REG->MIPITX_DSI_PLL_PWR, DA_DSI0_MPPLL_SDM_ISO_EN,
		  0);

	OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0, RG_DSI0_MPPLL_PREDIV,
		  0);
	OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0, RG_DSI0_MPPLL_POSDIV,
		  0);

	if (0 != data_Rate) {	/* if lcm_params->dsi.PLL_CLOCK=0, use other method */
		if (data_Rate > 1250) {
			pr_info("[dsi_drv.c error]Data Rate exceed limitation\n");
			ASSERT(0);
		} else if (data_Rate >= 500)
			txdiv = 1;
		else if (data_Rate >= 250)
			txdiv = 2;
		else if (data_Rate >= 125)
			txdiv = 4;
		else if (data_Rate > 62)
			txdiv = 8;
		else if (data_Rate >= 50)
			txdiv = 16;
		else {
			pr_err("[dsi_drv.c Error]: dataRate is too low,%d!!!\n", __LINE__);
			ASSERT(0);
		}
		/* PLL txdiv config */
		switch (txdiv) {
		case 1:
			OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0,
				  RG_DSI0_MPPLL_TXDIV0, 0);
			OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0,
				  RG_DSI0_MPPLL_TXDIV1, 0);
			break;
		case 2:
			OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0,
				  RG_DSI0_MPPLL_TXDIV0, 1);
			OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0,
				  RG_DSI0_MPPLL_TXDIV1, 0);
			break;
		case 4:
			OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0,
				  RG_DSI0_MPPLL_TXDIV0, 2);
			OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0,
				  RG_DSI0_MPPLL_TXDIV1, 0);
			break;
		case 8:
			OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0,
				  RG_DSI0_MPPLL_TXDIV0, 2);
			OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0,
				  RG_DSI0_MPPLL_TXDIV1, 1);
			break;
		case 16:
			OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0,
				  RG_DSI0_MPPLL_TXDIV0, 2);
			OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0,
				  RG_DSI0_MPPLL_TXDIV1, 2);
			break;
		default:
			break;
		}

		/* PLL PCW config */
		/*
		   PCW bit 24~30 = floor(pcw)
		   PCW bit 16~23 = (pcw - floor(pcw))*256
		   PCW bit 8~15 = (pcw*256 - floor(pcw)*256)*256
		   PCW bit 8~15 = (pcw*256*256 - floor(pcw)*256*256)*256
		 */
		/* pcw = data_Rate*4*txdiv/(26*2);//Post DIV =4, so need data_Rate*4 */
		pcw = data_Rate * txdiv / 13;

		OUTREGBIT(MIPITX_DSI_PLL_CON2_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON2,
			  RG_DSI0_MPPLL_SDM_PCW_H, (pcw & 0x7F));
		OUTREGBIT(MIPITX_DSI_PLL_CON2_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON2,
			  RG_DSI0_MPPLL_SDM_PCW_16_23,
			  ((256 * (data_Rate * txdiv % 13) / 13) & 0xFF));
		OUTREGBIT(MIPITX_DSI_PLL_CON2_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON2,
			  RG_DSI0_MPPLL_SDM_PCW_8_15,
			  ((256 * (256 * (data_Rate * txdiv % 13) % 13) / 13) & 0xFF));
		OUTREGBIT(MIPITX_DSI_PLL_CON2_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON2,
			  RG_DSI0_MPPLL_SDM_PCW_0_7,
			  ((256 * (256 * (256 * (data_Rate * txdiv % 13) % 13) % 13) / 13) & 0xFF));

		/* SSC config */
		/* pmod = ROUND(1000*26MHz/fmod/2);fmod default is 30Khz, and this value not be changed */
		/* pmod = 433.33; */
		if (1 != lcm_params->dsi.ssc_disable) {
			OUTREGBIT(MIPITX_DSI_PLL_CON1_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON1,
				  RG_DSI0_MPPLL_SDM_SSC_PH_INIT, 1);
			/* PRD=ROUND(pmod) = 433; */
			OUTREGBIT(MIPITX_DSI_PLL_CON1_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON1,
				  RG_DSI0_MPPLL_SDM_SSC_PRD, 0x1B1);
			if (0 != lcm_params->dsi.ssc_range)
				delta1 = lcm_params->dsi.ssc_range;

			ASSERT(delta1 <= 8);
			pdelta1 = (delta1 * data_Rate * txdiv * 262144 + 281664) / 563329;
			OUTREGBIT(MIPITX_DSI_PLL_CON3_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON3,
				  RG_DSI0_MPPLL_SDM_SSC_DELTA, pdelta1);
			OUTREGBIT(MIPITX_DSI_PLL_CON3_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON3,
				  RG_DSI0_MPPLL_SDM_SSC_DELTA1, pdelta1);
			/* OUTREGBIT(MIPITX_DSI_PLL_CON1_REG,DSI_PHY_REG->MIPITX_DSI_PLL_CON1,
			   RG_DSI0_MPPLL_SDM_FRA_EN,1); */
			pr_debug
			    ("[dsi_drv.c] PLL config:data_rate=%d,txdiv=%d,pcw=%d,delta1=%d,pdelta1=0x%x\n",
			     data_Rate, txdiv, DISP_REG_GET(&DSI_PHY_REG->MIPITX_DSI_PLL_CON2),
			     delta1, pdelta1);
		}
	} else {
		OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0,
			  RG_DSI0_MPPLL_TXDIV0, lcm_params->dsi.pll_div1);
		OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0,
			  RG_DSI0_MPPLL_TXDIV1, lcm_params->dsi.pll_div2);

		OUTREGBIT(MIPITX_DSI_PLL_CON2_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON2,
			  RG_DSI0_MPPLL_SDM_PCW_H, ((lcm_params->dsi.fbk_div) << 2));
		OUTREGBIT(MIPITX_DSI_PLL_CON2_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON2,
			  RG_DSI0_MPPLL_SDM_PCW_16_23, 0);
		OUTREGBIT(MIPITX_DSI_PLL_CON2_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON2,
			  RG_DSI0_MPPLL_SDM_PCW_8_15, 0);
		OUTREGBIT(MIPITX_DSI_PLL_CON2_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON2,
			  RG_DSI0_MPPLL_SDM_PCW_0_7, 0);

		/* OUTREGBIT(MIPITX_DSI_PLL_CON1_REG,DSI_PHY_REG->MIPITX_DSI_PLL_CON1,RG_DSI0_MPPLL_SDM_FRA_EN,0); */
	}
	OUTREGBIT(MIPITX_DSI_PLL_CON1_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON1,
		  RG_DSI0_MPPLL_SDM_FRA_EN, 1);

	OUTREGBIT(MIPITX_DSI0_CLOCK_LANE_REG, DSI_PHY_REG->MIPITX_DSI0_CLOCK_LANE,
		  RG_DSI0_LNTC_RT_CODE, 0x8);
	OUTREGBIT(MIPITX_DSI0_CLOCK_LANE_REG, DSI_PHY_REG->MIPITX_DSI0_CLOCK_LANE,
		  RG_DSI0_LNTC_PHI_SEL, 0x1);
	OUTREGBIT(MIPITX_DSI0_CLOCK_LANE_REG, DSI_PHY_REG->MIPITX_DSI0_CLOCK_LANE,
		  RG_DSI0_LNTC_LDOOUT_EN, 1);
	if (lcm_params->dsi.LANE_NUM > 0) {
		OUTREGBIT(MIPITX_DSI0_DATA_LANE0_REG, DSI_PHY_REG->MIPITX_DSI0_DATA_LANE0,
			  RG_DSI0_LNT0_RT_CODE, 0x8);
		OUTREGBIT(MIPITX_DSI0_DATA_LANE0_REG, DSI_PHY_REG->MIPITX_DSI0_DATA_LANE0,
			  RG_DSI0_LNT0_LDOOUT_EN, 1);
	}

	if (lcm_params->dsi.LANE_NUM > 1) {
		OUTREGBIT(MIPITX_DSI0_DATA_LANE1_REG, DSI_PHY_REG->MIPITX_DSI0_DATA_LANE1,
			  RG_DSI0_LNT1_RT_CODE, 0x8);
		OUTREGBIT(MIPITX_DSI0_DATA_LANE1_REG, DSI_PHY_REG->MIPITX_DSI0_DATA_LANE1,
			  RG_DSI0_LNT1_LDOOUT_EN, 1);
	}

	if (lcm_params->dsi.LANE_NUM > 2) {
		OUTREGBIT(MIPITX_DSI0_DATA_LANE2_REG, DSI_PHY_REG->MIPITX_DSI0_DATA_LANE2,
			  RG_DSI0_LNT2_RT_CODE, 0x8);
		OUTREGBIT(MIPITX_DSI0_DATA_LANE2_REG, DSI_PHY_REG->MIPITX_DSI0_DATA_LANE2,
			  RG_DSI0_LNT2_LDOOUT_EN, 1);
	}

	if (lcm_params->dsi.LANE_NUM > 3) {
		OUTREGBIT(MIPITX_DSI0_DATA_LANE3_REG, DSI_PHY_REG->MIPITX_DSI0_DATA_LANE3,
			  RG_DSI0_LNT3_RT_CODE, 0x8);
		OUTREGBIT(MIPITX_DSI0_DATA_LANE3_REG, DSI_PHY_REG->MIPITX_DSI0_DATA_LANE3,
			  RG_DSI0_LNT3_LDOOUT_EN, 1);
	}

	OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0, RG_DSI0_MPPLL_PLL_EN,
		  1);
	mdelay(1);
	if ((0 != data_Rate) && (1 != lcm_params->dsi.ssc_disable))
		OUTREGBIT(MIPITX_DSI_PLL_CON1_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON1,
			  RG_DSI0_MPPLL_SDM_SSC_EN, 1);
	else
		OUTREGBIT(MIPITX_DSI_PLL_CON1_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON1,
			  RG_DSI0_MPPLL_SDM_SSC_EN, 0);

	/* default POSDIV by 4 */
	OUTREGBIT(MIPITX_DSI_PLL_TOP_REG, DSI_PHY_REG->MIPITX_DSI_PLL_TOP, RG_MPPLL_PRESERVE_L, 3);
	OUTREGBIT(MIPITX_DSI_TOP_CON_REG, DSI_PHY_REG->MIPITX_DSI_TOP_CON, RG_DSI_PAD_TIE_LOW_EN,
		  0);
}


void DSI_PHY_clk_switch(bool on)
{
	if (on) {		/* workaround: do nothing */
		DSI_PHY_clk_setting(lcm_params);
	} else {
		/* pre_oe/oe = 1 */
		OUTREGBIT(MIPITX_DSI_SW_CTRL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_SW_CTRL_CON0,
			  SW_LNTC_LPTX_PRE_OE, 1);
		OUTREGBIT(MIPITX_DSI_SW_CTRL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_SW_CTRL_CON0,
			  SW_LNTC_LPTX_OE, 1);
		OUTREGBIT(MIPITX_DSI_SW_CTRL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_SW_CTRL_CON0,
			  SW_LNT0_LPTX_PRE_OE, 1);
		OUTREGBIT(MIPITX_DSI_SW_CTRL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_SW_CTRL_CON0,
			  SW_LNT0_LPTX_OE, 1);
		OUTREGBIT(MIPITX_DSI_SW_CTRL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_SW_CTRL_CON0,
			  SW_LNT1_LPTX_PRE_OE, 1);
		OUTREGBIT(MIPITX_DSI_SW_CTRL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_SW_CTRL_CON0,
			  SW_LNT1_LPTX_OE, 1);
		OUTREGBIT(MIPITX_DSI_SW_CTRL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_SW_CTRL_CON0,
			  SW_LNT2_LPTX_PRE_OE, 1);
		OUTREGBIT(MIPITX_DSI_SW_CTRL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_SW_CTRL_CON0,
			  SW_LNT2_LPTX_OE, 1);
		OUTREGBIT(MIPITX_DSI_SW_CTRL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_SW_CTRL_CON0,
			  SW_LNT3_LPTX_PRE_OE, 1);
		OUTREGBIT(MIPITX_DSI_SW_CTRL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_SW_CTRL_CON0,
			  SW_LNT3_LPTX_OE, 1);

		/* switch to mipi tx sw mode */
		OUTREGBIT(MIPITX_DSI_SW_CTRL_REG, DSI_PHY_REG->MIPITX_DSI_SW_CTRL, SW_CTRL_EN, 1);

		/* disable mipi clock */
		OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0,
			  RG_DSI0_MPPLL_PLL_EN, 0);
		mdelay(1);
		OUTREGBIT(MIPITX_DSI_PLL_TOP_REG, DSI_PHY_REG->MIPITX_DSI_PLL_TOP,
			  RG_MPPLL_PRESERVE_L, 0);

		OUTREGBIT(MIPITX_DSI_TOP_CON_REG, DSI_PHY_REG->MIPITX_DSI_TOP_CON,
			  RG_DSI_PAD_TIE_LOW_EN, 1);
		OUTREGBIT(MIPITX_DSI0_CLOCK_LANE_REG, DSI_PHY_REG->MIPITX_DSI0_CLOCK_LANE,
			  RG_DSI0_LNTC_LDOOUT_EN, 0);
		OUTREGBIT(MIPITX_DSI0_DATA_LANE0_REG, DSI_PHY_REG->MIPITX_DSI0_DATA_LANE0,
			  RG_DSI0_LNT0_LDOOUT_EN, 0);
		OUTREGBIT(MIPITX_DSI0_DATA_LANE1_REG, DSI_PHY_REG->MIPITX_DSI0_DATA_LANE1,
			  RG_DSI0_LNT1_LDOOUT_EN, 0);
		OUTREGBIT(MIPITX_DSI0_DATA_LANE2_REG, DSI_PHY_REG->MIPITX_DSI0_DATA_LANE2,
			  RG_DSI0_LNT2_LDOOUT_EN, 0);
		OUTREGBIT(MIPITX_DSI0_DATA_LANE3_REG, DSI_PHY_REG->MIPITX_DSI0_DATA_LANE3,
			  RG_DSI0_LNT3_LDOOUT_EN, 0);
		mdelay(1);

		OUTREGBIT(MIPITX_DSI_PLL_PWR_REG, DSI_PHY_REG->MIPITX_DSI_PLL_PWR,
			  DA_DSI0_MPPLL_SDM_ISO_EN, 1);
		OUTREGBIT(MIPITX_DSI_PLL_PWR_REG, DSI_PHY_REG->MIPITX_DSI_PLL_PWR,
			  DA_DSI0_MPPLL_SDM_PWR_ON, 0);
		OUTREGBIT(MIPITX_DSI_TOP_CON_REG, DSI_PHY_REG->MIPITX_DSI_TOP_CON,
			  RG_DSI_LNT_HS_BIAS_EN, 0);

		OUTREGBIT(MIPITX_DSI0_CON_REG, DSI_PHY_REG->MIPITX_DSI0_CON, RG_DSI0_CKG_LDOOUT_EN,
			  0);
		OUTREGBIT(MIPITX_DSI0_CON_REG, DSI_PHY_REG->MIPITX_DSI0_CON, RG_DSI0_LDOCORE_EN, 0);

		OUTREGBIT(MIPITX_DSI_BG_CON_REG, DSI_PHY_REG->MIPITX_DSI_BG_CON, RG_DSI_BG_CKEN, 0);
		OUTREGBIT(MIPITX_DSI_BG_CON_REG, DSI_PHY_REG->MIPITX_DSI_BG_CON, RG_DSI_BG_CORE_EN,
			  0);

		OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0,
			  RG_DSI0_MPPLL_PREDIV, 0);
		OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0,
			  RG_DSI0_MPPLL_TXDIV0, 0);
		OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0,
			  RG_DSI0_MPPLL_TXDIV1, 0);
		OUTREGBIT(MIPITX_DSI_PLL_CON0_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON0,
			  RG_DSI0_MPPLL_POSDIV, 0);


		OUTREG32(&DSI_PHY_REG->MIPITX_DSI_PLL_CON1, 0x00000000);
		OUTREG32(&DSI_PHY_REG->MIPITX_DSI_PLL_CON2, 0x50000000);

		OUTREGBIT(MIPITX_DSI_SW_CTRL_REG, DSI_PHY_REG->MIPITX_DSI_SW_CTRL, SW_CTRL_EN, 0);
		mdelay(1);
	}
}

void DSI_Set_VM_CMD(LCM_PARAMS *lcm_params)
{
	OUTREGBIT(DSI_VM_CMD_CON_REG, DSI_REG->DSI_VM_CMD_CON, TS_VFP_EN, 1);
	OUTREGBIT(DSI_VM_CMD_CON_REG, DSI_REG->DSI_VM_CMD_CON, VM_CMD_EN, 1);
}

void DSI_PHY_TIMCONFIG(LCM_PARAMS *lcm_params)
{
	DSI_PHY_TIMCON0_REG timcon0;
	DSI_PHY_TIMCON1_REG timcon1;
	DSI_PHY_TIMCON2_REG timcon2;
	DSI_PHY_TIMCON3_REG timcon3;
	unsigned int div1 = 0;
	unsigned int div2 = 0;
	unsigned int pre_div = 0;
	unsigned int post_div = 0;
	unsigned int fbk_sel = 0;
	unsigned int fbk_div = 0;
	unsigned int lane_no = lcm_params->dsi.LANE_NUM;

	/* unsigned int div2_real; */
	unsigned int cycle_time;
	unsigned int ui;
	unsigned int hs_trail_m, hs_trail_n;

	if (0 != lcm_params->dsi.PLL_CLOCK) {
		ui = 1000 / (lcm_params->dsi.PLL_CLOCK * 2) + 0x01;
		cycle_time = 8000 / (lcm_params->dsi.PLL_CLOCK * 2) + 0x01;
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI",
			       "DSI_PHY, Cycle Time: %d(ns), Unit Interval: %d(ns). , lane#: %d\n",
			       cycle_time, ui, lane_no);
	} else {
		div1 = lcm_params->dsi.pll_div1;
		div2 = lcm_params->dsi.pll_div2;
		fbk_div = lcm_params->dsi.fbk_div;
		switch (div1) {
		case 0:
			div1 = 1;
			break;

		case 1:
			div1 = 2;
			break;

		case 2:
		case 3:
			div1 = 4;
			break;

		default:
			pr_info("div1 should be less than 4!!\n");
			div1 = 4;
			break;
		}

		switch (div2) {
		case 0:
			div2 = 1;
			break;
		case 1:
			div2 = 2;
			break;
		case 2:
		case 3:
			div2 = 4;
			break;
		default:
			pr_info("div2 should be less than 4!!\n");
			div2 = 4;
			break;
		}

		switch (pre_div) {
		case 0:
			pre_div = 1;
			break;

		case 1:
			pre_div = 2;
			break;

		case 2:
		case 3:
			pre_div = 4;
			break;

		default:
			pr_info("pre_div should be less than 4!!\n");
			pre_div = 4;
			break;
		}

		switch (post_div) {
		case 0:
			post_div = 1;
			break;

		case 1:
			post_div = 2;
			break;

		case 2:
		case 3:
			post_div = 4;
			break;

		default:
			pr_info("post_div should be less than 4!!\n");
			post_div = 4;
			break;
		}

		switch (fbk_sel) {
		case 0:
			fbk_sel = 1;
			break;

		case 1:
			fbk_sel = 2;
			break;

		case 2:
		case 3:
			fbk_sel = 4;
			break;

		default:
			pr_info("fbk_sel should be less than 4!!\n");
			fbk_sel = 4;
			break;
		}
		cycle_time = (1000 * 4 * div2 * div1) / (fbk_div * 26) + 0x01;

		ui = (1000 * div2 * div1) / (fbk_div * 26 * 0x2) + 0x01;
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI",
			       "DSI_PHY, Cycle Time: %d(ns), Unit Interval: %d(ns). div1: %d, div2: %d, fbk_div: %d, lane#: %d\n",
			       cycle_time, ui, div1, div2, fbk_div, lane_no);
	}

	/* div2_real=div2 ? div2*0x02 : 0x1; */
	/* cycle_time = (1000 * div2 * div1 * pre_div * post_div)/ (fbk_sel * (fbk_div+0x01) * 26) + 1; */
	/* ui = (1000 * div2 * div1 * pre_div * post_div)/ (fbk_sel * (fbk_div+0x01) * 26 * 2) + 1; */
#define NS_TO_CYCLE(n, c)   ((n) / (c))

	hs_trail_m = 1;
	hs_trail_n =
	    (lcm_params->dsi.HS_TRAIL == 0) ? NS_TO_CYCLE(((hs_trail_m * 0x4) + 0x60),
							  cycle_time) : lcm_params->dsi.HS_TRAIL;
	/* +3 is recommended from designer becauase of HW latency */
	timcon0.HS_TRAIL = ((hs_trail_m > hs_trail_n) ? hs_trail_m : hs_trail_n) + 0x0a;

	timcon0.HS_PRPR =
	    (lcm_params->dsi.HS_PRPR == 0) ? NS_TO_CYCLE((0x40 + 0x5 * ui),
							 cycle_time) : lcm_params->dsi.HS_PRPR;
	/* HS_PRPR can't be 1. */
	if (timcon0.HS_PRPR == 0)
		timcon0.HS_PRPR = 1;

	timcon0.HS_ZERO =
	    (lcm_params->dsi.HS_ZERO == 0) ? NS_TO_CYCLE((0xC8 + 0x0a * ui),
							 cycle_time) : lcm_params->dsi.HS_ZERO;
	if (timcon0.HS_ZERO > timcon0.HS_PRPR)
		timcon0.HS_ZERO -= timcon0.HS_PRPR;

	timcon0.LPX =
	    (lcm_params->dsi.LPX == 0) ? NS_TO_CYCLE(0x50, cycle_time) : lcm_params->dsi.LPX;
	if (timcon0.LPX == 0)
		timcon0.LPX = 1;

	/* timcon1.TA_SACK     = (lcm_params->dsi.TA_SACK == 0) ? 1 : lcm_params->dsi.TA_SACK; */
	timcon1.TA_GET =
	    (lcm_params->dsi.TA_GET == 0) ? (0x5 * timcon0.LPX) : lcm_params->dsi.TA_GET;
	timcon1.TA_SURE =
	    (lcm_params->dsi.TA_SURE == 0) ? (0x3 * timcon0.LPX / 0x2) : lcm_params->dsi.TA_SURE;
	timcon1.TA_GO = (lcm_params->dsi.TA_GO == 0) ? (0x4 * timcon0.LPX) : lcm_params->dsi.TA_GO;
	/* -------------------------------------------------------------- */
	/* NT35510 need fine tune timing */
	/* Data_hs_exit = 60 ns + 128UI */
	/* Clk_post = 60 ns + 128 UI. */
	/* -------------------------------------------------------------- */
	timcon1.DA_HS_EXIT =
	    (lcm_params->dsi.DA_HS_EXIT == 0) ? NS_TO_CYCLE((0x3c + 0x80 * ui),
							    cycle_time) : lcm_params->dsi.
	    DA_HS_EXIT;

	timcon2.CLK_TRAIL =
	    ((lcm_params->dsi.CLK_TRAIL == 0) ? NS_TO_CYCLE(0x64,
							    cycle_time) : lcm_params->dsi.
	     CLK_TRAIL) + 0x0a;
	/* CLK_TRAIL can't be 1. */
	if (timcon2.CLK_TRAIL < 2)
		timcon2.CLK_TRAIL = 2;

	/* timcon2.LPX_WAIT    = (lcm_params->dsi.LPX_WAIT == 0) ? 1 : lcm_params->dsi.LPX_WAIT; */
	timcon2.CONT_DET = lcm_params->dsi.CONT_DET;
	timcon2.CLK_ZERO =
	    (lcm_params->dsi.CLK_ZERO == 0) ? NS_TO_CYCLE(0x190,
							  cycle_time) : lcm_params->dsi.CLK_ZERO;

	timcon3.CLK_HS_PRPR =
	    (lcm_params->dsi.CLK_HS_PRPR == 0) ? NS_TO_CYCLE(0x40,
							     cycle_time) : lcm_params->dsi.
	    CLK_HS_PRPR;
	if (timcon3.CLK_HS_PRPR == 0)
		timcon3.CLK_HS_PRPR = 1;
	timcon3.CLK_HS_EXIT =
	    (lcm_params->dsi.CLK_HS_EXIT == 0) ? (2 * timcon0.LPX) : lcm_params->dsi.CLK_HS_EXIT;
	timcon3.CLK_HS_POST =
	    (lcm_params->dsi.CLK_HS_POST == 0) ? NS_TO_CYCLE((0x3c + 0x80 * ui),
							     cycle_time) : lcm_params->dsi.
	    CLK_HS_POST;

	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI",
		       "DSI_PHY, HS_TRAIL:%d, HS_ZERO:%d, HS_PRPR:%d, LPX:%d, TA_GET:%d, TA_SURE:%d\n",
		       timcon0.HS_TRAIL, timcon0.HS_ZERO, timcon0.HS_PRPR, timcon0.LPX,
		       timcon1.TA_GET, timcon1.TA_SURE);
	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI",
		       "DSI_PHY, TA_GO:%d, CLK_TRAIL:%d, CLK_ZERO:%d, CLK_HS_PRPR:%d\n",
		       timcon1.TA_GO, timcon2.CLK_TRAIL, timcon2.CLK_ZERO, timcon3.CLK_HS_PRPR);

	OUTREGBIT(DSI_PHY_TIMCON0_REG, DSI_REG->DSI_PHY_TIMECON0, LPX, timcon0.LPX);
	OUTREGBIT(DSI_PHY_TIMCON0_REG, DSI_REG->DSI_PHY_TIMECON0, HS_PRPR, timcon0.HS_PRPR);
	OUTREGBIT(DSI_PHY_TIMCON0_REG, DSI_REG->DSI_PHY_TIMECON0, HS_ZERO, timcon0.HS_ZERO);
	OUTREGBIT(DSI_PHY_TIMCON0_REG, DSI_REG->DSI_PHY_TIMECON0, HS_TRAIL, timcon0.HS_TRAIL);

	OUTREGBIT(DSI_PHY_TIMCON1_REG, DSI_REG->DSI_PHY_TIMECON1, TA_GO, timcon1.TA_GO);
	OUTREGBIT(DSI_PHY_TIMCON1_REG, DSI_REG->DSI_PHY_TIMECON1, TA_SURE, timcon1.TA_SURE);
	OUTREGBIT(DSI_PHY_TIMCON1_REG, DSI_REG->DSI_PHY_TIMECON1, TA_GET, timcon1.TA_GET);
	OUTREGBIT(DSI_PHY_TIMCON1_REG, DSI_REG->DSI_PHY_TIMECON1, DA_HS_EXIT, timcon1.DA_HS_EXIT);

	OUTREGBIT(DSI_PHY_TIMCON2_REG, DSI_REG->DSI_PHY_TIMECON2, CONT_DET, timcon2.CONT_DET);
	OUTREGBIT(DSI_PHY_TIMCON2_REG, DSI_REG->DSI_PHY_TIMECON2, CLK_ZERO, timcon2.CLK_ZERO);
	OUTREGBIT(DSI_PHY_TIMCON2_REG, DSI_REG->DSI_PHY_TIMECON2, CLK_TRAIL, timcon2.CLK_TRAIL);

	OUTREGBIT(DSI_PHY_TIMCON3_REG, DSI_REG->DSI_PHY_TIMECON3, CLK_HS_PRPR, timcon3.CLK_HS_PRPR);
	OUTREGBIT(DSI_PHY_TIMCON3_REG, DSI_REG->DSI_PHY_TIMECON3, CLK_HS_POST, timcon3.CLK_HS_POST);
	OUTREGBIT(DSI_PHY_TIMCON3_REG, DSI_REG->DSI_PHY_TIMECON3, CLK_HS_EXIT, timcon3.CLK_HS_EXIT);
	pr_debug("%s, 0x%08x,0x%08x,0x%08x,0x%08x\n", __func__,
		 DISP_REG_GET(DDP_REG_BASE_DSI + 0x110), DISP_REG_GET(DDP_REG_BASE_DSI + 0x114),
		 DISP_REG_GET(DDP_REG_BASE_DSI + 0x118), DISP_REG_GET(DDP_REG_BASE_DSI + 0x11c));
}



void DSI_clk_ULP_mode(bool enter)
{
	if (enter) {
		OUTREGBIT(DSI_PHY_LCCON_REG, DSI_REG->DSI_PHY_LCCON, LC_HS_TX_EN, 0);
		mdelay(1);

		OUTREGBIT(DSI_PHY_LCCON_REG, DSI_REG->DSI_PHY_LCCON, LC_ULPM_EN, 1);
		mdelay(1);
	} else {
		OUTREGBIT(DSI_PHY_LCCON_REG, DSI_REG->DSI_PHY_LCCON, LC_ULPM_EN, 0);
		mdelay(1);

		OUTREGBIT(DSI_PHY_LCCON_REG, DSI_REG->DSI_PHY_LCCON, LC_WAKEUP_EN, 1);
		mdelay(1);

		OUTREGBIT(DSI_PHY_LCCON_REG, DSI_REG->DSI_PHY_LCCON, LC_WAKEUP_EN, 0);
		mdelay(1);
	}
}


void DSI_clk_HS_mode(bool enter)
{
	DSI_PHY_LCCON_REG tmp_reg1 = DSI_REG->DSI_PHY_LCCON;


	if (enter && !DSI_clk_HS_state()) {
		tmp_reg1.LC_HS_TX_EN = 1;
		OUTREG32(&DSI_REG->DSI_PHY_LCCON, AS_UINT32(&tmp_reg1));
	} else if (!enter && DSI_clk_HS_state()) {
		tmp_reg1.LC_HS_TX_EN = 0;
		OUTREG32(&DSI_REG->DSI_PHY_LCCON, AS_UINT32(&tmp_reg1));

	}
}


void DSI_Continuous_HS(void)
{
	DSI_TXRX_CTRL_REG tmp_reg = DSI_REG->DSI_TXRX_CTRL;

	tmp_reg.HSTX_CKLP_EN = 0;
	OUTREG32(&DSI_REG->DSI_TXRX_CTRL, AS_UINT32(&tmp_reg));
}


bool DSI_clk_HS_state(void)
{
	return DSI_REG->DSI_PHY_LCCON.LC_HS_TX_EN ? true : false;
}


void DSI_lane0_ULP_mode(bool enter)
{
	DSI_PHY_LD0CON_REG tmp_reg1;

	tmp_reg1 = DSI_REG->DSI_PHY_LD0CON;

	if (enter) {
		/* suspend */
		tmp_reg1.L0_HS_TX_EN = 0;
		OUTREG32(&DSI_REG->DSI_PHY_LD0CON, AS_UINT32(&tmp_reg1));
		lcm_mdelay(1);
		tmp_reg1.L0_ULPM_EN = 1;
		OUTREG32(&DSI_REG->DSI_PHY_LD0CON, AS_UINT32(&tmp_reg1));
		lcm_mdelay(1);
	} else {
		/* resume */
		tmp_reg1.L0_ULPM_EN = 0;
		OUTREG32(&DSI_REG->DSI_PHY_LD0CON, AS_UINT32(&tmp_reg1));
		lcm_mdelay(1);
		tmp_reg1.L0_WAKEUP_EN = 1;
		OUTREG32(&DSI_REG->DSI_PHY_LD0CON, AS_UINT32(&tmp_reg1));
		lcm_mdelay(1);
		tmp_reg1.L0_WAKEUP_EN = 0;
		OUTREG32(&DSI_REG->DSI_PHY_LD0CON, AS_UINT32(&tmp_reg1));
		lcm_mdelay(1);
	}
}

/* called by DPI ISR */
void DSI_handle_esd_recovery(void)
{
}


/* called by "esd_recovery_kthread" */
bool DSI_esd_check(void)
{
#ifndef MT65XX_NEW_DISP
	bool result = false;

	if (dsi_esd_recovery)
		result = true;
	else
		result = false;

	dsi_esd_recovery = false;
#else
	DSI_MODE_CTRL_REG mode_ctl, mode_ctl_backup;
	bool result = false;
#if ENABLE_DSI_INTERRUPT	/* wait video mode done */
	static const long WAIT_TIMEOUT = HZ / 2;	/* 2 sec//modified to 500ms */
	long ret;
#endif
	/* backup video mode */
	OUTREG32(&mode_ctl_backup, AS_UINT32(&DSI_REG->DSI_MODE_CTRL));
	OUTREG32(&mode_ctl, AS_UINT32(&DSI_REG->DSI_MODE_CTRL));
	/* set to cmd mode */
	mode_ctl.MODE = 0;
	OUTREG32(&DSI_REG->DSI_MODE_CTRL, AS_UINT32(&mode_ctl));
#if ENABLE_DSI_INTERRUPT	/* wait video mode done */

	wait_vm_done_irq = true;
	ret = wait_event_interruptible_timeout(_dsi_wait_vm_done_queue,
					       !_IsEngineBusy(), WAIT_TIMEOUT);
	if (0 == ret) {
		DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
			       " Wait for DSI engine read ready timeout!!!\n");

		DSI_DumpRegisters();
		/* /do necessary reset here */
		DSI_Reset();
		wait_vm_done_irq = false;
		return 0;
	}
#else
	unsigned int read_timeout_ms = 100;
#ifdef DDI_DRV_DEBUG_LOG_ENABLE
	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " Start polling VM done ready!!!\n");
#endif
	while (DSI_REG->DSI_INTSTA.VM_DONE == 0) {
		/* /keep polling */
		msleep(20);
		read_timeout_ms--;

		if (read_timeout_ms == 0) {
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI",
				       " Polling DSI VM done timeout!!!\n");
			DSI_DumpRegisters();

			DSI_Reset();
			return 0;
		}
	}
	/* DSI_REG->DSI_INTSTA.VM_DONE = 0; */
	OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, VM_DONE, 0);
#ifdef DDI_DRV_DEBUG_LOG_ENABLE
	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " End polling DSI VM done ready!!!\n");
#endif
#endif
	/* read DriverIC and check ESD */
	result = lcm_drv->esd_check();
	/* restore video mode */
	if (!result)
		OUTREG32(&DSI_REG->DSI_MODE_CTRL, AS_UINT32(&mode_ctl_backup));
#endif
	wait_vm_done_irq = false;
	return result;
}


void DSI_set_int_TE(bool enable, unsigned int period)
{
#ifndef MT65XX_NEW_DISP
	dsi_int_te_enabled = enable;
	if (period < 1)
		period = 1;
	dsi_int_te_period = period;
	dsi_dpi_isr_count = 0;
#endif
}


/* called by DPI ISR. */
bool DSI_handle_int_TE(void)
{
#ifndef CONFIG_ARCH_MT8127
#ifndef MT65XX_NEW_DISP
	DSI_T0_INS t0;
	long int dsi_current_time;

	if (!DSI_REG->DSI_MODE_CTRL.MODE)
		return false;

	dsi_current_time = get_current_time_us();

	if (DSI_REG->DSI_STATE_DBG3.TCON_STATE == DSI_VDO_VFP_STATE) {
		udelay(_dsiContext.vfp_period_us / 2);

		if ((DSI_REG->DSI_STATE_DBG3.TCON_STATE == DSI_VDO_VFP_STATE)
		    && DSI_REG->DSI_STATE_DBG0.CTL_STATE_0 == 0x1) {
			/* Can't do int. TE check while INUSE FB number is not 0 because later disable/enable
			   DPI will set INUSE FB to number 0. */
			if (DPI_REG->STATUS.FB_INUSE != 0)
				return false;

			DSI_clk_HS_mode(0);

			/* DSI_REG->DSI_COM_CTRL.DSI_RESET = 1; */
			OUTREGBIT(DSI_COM_CTRL_REG, DSI_REG->DSI_COM_CTRL, DSI_RESET, 1);
			DPI_DisableClk();
			DSI_SetMode(CMD_MODE);
			/* DSI_REG->DSI_COM_CTRL.DSI_RESET = 0; */
			OUTREGBIT(DSI_COM_CTRL_REG, DSI_REG->DSI_COM_CTRL, DSI_RESET, 0);
			/* DSI_Reset(); */

			t0.CONFG = 0x20;	/* /TE */
			t0.Data0 = 0;
			t0.Data_ID = 0;
			t0.Data1 = 0;

			OUTREG32(&DSI_CMDQ_REG->data[0], AS_UINT32(&t0));
			OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);

			/* Enable RD_RDY INT for polling it's status later */
			/* DSI_REG->DSI_INTEN.RD_RDY =  1; */
			OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, RD_RDY, 1);

			DSI_Start();

			while (DSI_REG->DSI_INTSTA.RD_RDY == 0) {
				if (get_current_time_us() - dsi_current_time >
				    _dsiContext.vfp_period_us) {
					DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
						       " Wait for internal TE time-out for %d (us)!!!\n",
						       _dsiContext.vfp_period_us);

					/* /do necessary reset here */
					/* DSI_REG->DSI_RACK.DSI_RACK = 1; */
					OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);
					DSI_Reset();

					return true;
				}
			}

			/* Write clear RD_RDY */
			/* DSI_REG->DSI_INTSTA.RD_RDY = 1; */
			/* DSI_REG->DSI_RACK.DSI_RACK = 1; */
			OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, RD_RDY, 1);
			OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);
			/* Write clear CMD_DONE */
			/* DSI_REG->DSI_INTSTA.CMD_DONE = 1; */
			OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, CMD_DONE, 1);

			/* Restart video mode. (with VSA ahead) */
			DSI_SetMode(SYNC_PULSE_VDO_MODE);
			DSI_clk_HS_mode(1);
			DPI_EnableClk();
			DSI_Start();
		}

	}
#endif
#endif
	return false;

}


void DSI_set_noncont_clk(bool enable, unsigned int period)
{
	dsi_noncont_clk_enabled = enable;
/* dsi_noncont_clk_period = period; */
}

void DSI_Detect_glitch_enable(bool enable)
{
	dsi_glitch_enable = enable;
}

/* called by DPI ISR. */
void DSI_handle_noncont_clk(void)
{
#ifndef CONFIG_ARCH_MT8127
#ifndef MT65XX_NEW_DISP
	unsigned int state;
	long int dsi_current_time;

	if (!DSI_REG->DSI_MODE_CTRL.MODE)
		return;

	state = DSI_REG->DSI_STATE_DBG3.TCON_STATE;

	dsi_current_time = get_current_time_us();

	switch (state) {
	case DSI_VDO_VSA_VS_STATE:
		while (DSI_REG->DSI_STATE_DBG3.TCON_STATE != DSI_VDO_VSA_HS_STATE) {
			if (get_current_time_us() - dsi_current_time > _dsiContext.vsa_vs_period_us) {
				DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
					       " Wait for %x state timeout %d (us)!!!\n",
					       DSI_VDO_VSA_HS_STATE, _dsiContext.vsa_vs_period_us);
				return;
			}
		}
		break;

	case DSI_VDO_VSA_HS_STATE:
		while (DSI_REG->DSI_STATE_DBG3.TCON_STATE != DSI_VDO_VSA_VE_STATE) {
			if (get_current_time_us() - dsi_current_time > _dsiContext.vsa_hs_period_us) {
				DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
					       " Wait for %x state timeout %d (us)!!!\n",
					       DSI_VDO_VSA_VE_STATE, _dsiContext.vsa_hs_period_us);
				return;
			}
		}
		break;

	case DSI_VDO_VSA_VE_STATE:
		while (DSI_REG->DSI_STATE_DBG3.TCON_STATE != DSI_VDO_VBP_STATE) {
			if (get_current_time_us() - dsi_current_time > _dsiContext.vsa_ve_period_us) {
				DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
					       " Wait for %x state timeout %d (us)!!!\n",
					       DSI_VDO_VBP_STATE, _dsiContext.vsa_ve_period_us);
				return;
			}
		}
		break;

	case DSI_VDO_VBP_STATE:
		DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
			       "Can't do clock switch in DSI_VDO_VBP_STATE !!!\n");
		break;

	case DSI_VDO_VACT_STATE:
		while (DSI_REG->DSI_STATE_DBG3.TCON_STATE != DSI_VDO_VFP_STATE) {
			if (get_current_time_us() - dsi_current_time > _dsiContext.vfp_period_us) {
				DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
					       " Wait for %x state timeout %d (us)!!!\n",
					       DSI_VDO_VFP_STATE, _dsiContext.vfp_period_us);
				return;
			}
		}
		break;

	case DSI_VDO_VFP_STATE:
		while (DSI_REG->DSI_STATE_DBG3.TCON_STATE != DSI_VDO_VSA_VS_STATE) {
			if (get_current_time_us() - dsi_current_time > _dsiContext.vfp_period_us) {
				DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
					       " Wait for %x state timeout %d (us)!!!\n",
					       DSI_VDO_VSA_VS_STATE, _dsiContext.vfp_period_us);
				return;
			}
		}
		break;

	default:
		DISP_LOG_PRINT(ANDROID_LOG_ERROR, "DSI", "invalid state = %x\n", state);
		return;
	}

	/* Clock switch HS->LP->HS */
	DSI_clk_HS_mode(0);
	udelay(1);
	DSI_clk_HS_mode(1);
#endif
#endif
}

#ifdef ENABLE_DSI_ERROR_REPORT
static unsigned int _dsi_cmd_queue[32];
#endif
void DSI_set_cmdq_V2(unsigned cmd, unsigned char count, unsigned char *para_list,
		     unsigned char force_update)
{
	uint32_t i;
	uint32_t goto_addr, mask_para, set_para;
	/* uint32_t fbPhysAddr, fbVirAddr; */
	DSI_T0_INS t0;
	DSI_T2_INS t2;

	if (0 != DSI_REG->DSI_MODE_CTRL.MODE) {	/* not in cmd mode */
		DSI_VM_CMD_CON_REG vm_cmdq;

		OUTREG32(&vm_cmdq, AS_UINT32(&DSI_REG->DSI_VM_CMD_CON));
		pr_info("set cmdq in VDO mode in set_cmdq_V2\n");
		if (cmd < 0xB0) {
			if (count > 1) {
				vm_cmdq.LONG_PKT = 1;
				vm_cmdq.CM_DATA_ID = DSI_DCS_LONG_PACKET_ID;
				vm_cmdq.CM_DATA_0 = count + 1;
				OUTREG32(&DSI_REG->DSI_VM_CMD_CON, AS_UINT32(&vm_cmdq));

				goto_addr = (uint32_t) (&DSI_VM_CMD_REG->data[0].byte0);
				mask_para = (0xFF << ((goto_addr & 0x3) * 8));
				set_para = (cmd << ((goto_addr & 0x3) * 8));
				MASKREG32(goto_addr & (~0x3), mask_para, set_para);

				for (i = 0; i < count; i++) {
					goto_addr = (uint32_t) (&DSI_VM_CMD_REG->data[0].byte1) + i;
					mask_para = (0xFF << ((goto_addr & 0x3) * 8));
					set_para = (para_list[i] << ((goto_addr & 0x3) * 8));
					MASKREG32(goto_addr & (~0x3), mask_para, set_para);
				}
			} else {
				vm_cmdq.LONG_PKT = 0;
				vm_cmdq.CM_DATA_0 = cmd;
				if (count) {
					vm_cmdq.CM_DATA_ID = DSI_DCS_SHORT_PACKET_ID_1;
					vm_cmdq.CM_DATA_1 = para_list[0];
				} else {
					vm_cmdq.CM_DATA_ID = DSI_DCS_SHORT_PACKET_ID_0;
					vm_cmdq.CM_DATA_1 = 0;
				}
				OUTREG32(&DSI_REG->DSI_VM_CMD_CON, AS_UINT32(&vm_cmdq));
			}
		} else {
			if (count > 1) {
				vm_cmdq.LONG_PKT = 1;
				vm_cmdq.CM_DATA_ID = DSI_GERNERIC_LONG_PACKET_ID;
				vm_cmdq.CM_DATA_0 = count + 1;
				OUTREG32(&DSI_REG->DSI_VM_CMD_CON, AS_UINT32(&vm_cmdq));

				goto_addr = (uint32_t) (&DSI_VM_CMD_REG->data[0].byte0);
				mask_para = (0xFF << ((goto_addr & 0x3) * 8));
				set_para = (cmd << ((goto_addr & 0x3) * 8));
				MASKREG32(goto_addr & (~0x3), mask_para, set_para);

				for (i = 0; i < count; i++) {
					goto_addr = (uint32_t) (&DSI_VM_CMD_REG->data[0].byte1) + i;
					mask_para = (0xFF << ((goto_addr & 0x3) * 8));
					set_para = (para_list[i] << ((goto_addr & 0x3) * 8));
					MASKREG32(goto_addr & (~0x3), mask_para, set_para);
				}
			} else {
				vm_cmdq.LONG_PKT = 0;
				vm_cmdq.CM_DATA_0 = cmd;
				if (count) {
					vm_cmdq.CM_DATA_ID = DSI_GERNERIC_SHORT_PACKET_ID_2;
					vm_cmdq.CM_DATA_1 = para_list[0];
				} else {
					vm_cmdq.CM_DATA_ID = DSI_GERNERIC_SHORT_PACKET_ID_1;
					vm_cmdq.CM_DATA_1 = 0;
				}
				OUTREG32(&DSI_REG->DSI_VM_CMD_CON, AS_UINT32(&vm_cmdq));
			}
		}
		/* start DSI VM CMDQ */
		if (force_update) {
			MMProfileLogEx(MTKFB_MMP_Events.DSICmd, MMProfileFlagStart,
				       *(unsigned int *)(&DSI_VM_CMD_REG->data[0]),
				       *(unsigned int *)(&DSI_VM_CMD_REG->data[1]));
			DSI_EnableVM_CMD();

			/* must wait VM CMD done? */
			MMProfileLogEx(MTKFB_MMP_Events.DSICmd, MMProfileFlagEnd,
				       *(unsigned int *)(&DSI_VM_CMD_REG->data[2]),
				       *(unsigned int *)(&DSI_VM_CMD_REG->data[3]));
		}
	} else {
#ifdef ENABLE_DSI_ERROR_REPORT
		if ((para_list[0] & 1)) {
			memset(_dsi_cmd_queue, 0, sizeof(_dsi_cmd_queue));
			memcpy(_dsi_cmd_queue, para_list, count);
			_dsi_cmd_queue[(count + 3) / 4 * 4] = 0x4;
			count = (count + 3) / 4 * 4 + 4;
			para_list = (unsigned char *)_dsi_cmd_queue;
		} else {
			para_list[0] |= 4;
		}
#endif
		_WaitForEngineNotBusy();
		if (cmd < 0xB0) {
			if (count > 1) {
				t2.CONFG = 2;
				t2.Data_ID = DSI_DCS_LONG_PACKET_ID;
				t2.WC16 = count + 1;

				OUTREG32(&DSI_CMDQ_REG->data[0], AS_UINT32(&t2));

				goto_addr = (uint32_t) (&DSI_CMDQ_REG->data[1].byte0);
				mask_para = (0xFF << ((goto_addr & 0x3) * 8));
				set_para = (cmd << ((goto_addr & 0x3) * 8));
				MASKREG32(goto_addr & (~0x3), mask_para, set_para);

				for (i = 0; i < count; i++) {
					goto_addr = (uint32_t) (&DSI_CMDQ_REG->data[1].byte1) + i;
					mask_para = (0xFF << ((goto_addr & 0x3) * 8));
					set_para = (para_list[i] << ((goto_addr & 0x3) * 8));
					MASKREG32(goto_addr & (~0x3), mask_para, set_para);
				}

				OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 2 + (count) / 4);
			} else {
				t0.CONFG = 0;
				t0.Data0 = cmd;
				if (count) {
					t0.Data_ID = DSI_DCS_SHORT_PACKET_ID_1;
					t0.Data1 = para_list[0];
				} else {
					t0.Data_ID = DSI_DCS_SHORT_PACKET_ID_0;
					t0.Data1 = 0;
				}
				OUTREG32(&DSI_CMDQ_REG->data[0], AS_UINT32(&t0));
				OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);
			}
		} else {
			if (count > 1) {
				t2.CONFG = 2;
				t2.Data_ID = DSI_GERNERIC_LONG_PACKET_ID;
				t2.WC16 = count + 1;

				OUTREG32(&DSI_CMDQ_REG->data[0], AS_UINT32(&t2));

				goto_addr = (uint32_t) (&DSI_CMDQ_REG->data[1].byte0);
				mask_para = (0xFF << ((goto_addr & 0x3) * 8));
				set_para = (cmd << ((goto_addr & 0x3) * 8));
				MASKREG32(goto_addr & (~0x3), mask_para, set_para);

				for (i = 0; i < count; i++) {
					goto_addr = (uint32_t) (&DSI_CMDQ_REG->data[1].byte1) + i;
					mask_para = (0xFF << ((goto_addr & 0x3) * 8));
					set_para = (para_list[i] << ((goto_addr & 0x3) * 8));
					MASKREG32(goto_addr & (~0x3), mask_para, set_para);
				}

				OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 2 + (count) / 4);

			} else {
				t0.CONFG = 0;
				t0.Data0 = cmd;
				if (count) {
					t0.Data_ID = DSI_GERNERIC_SHORT_PACKET_ID_2;
					t0.Data1 = para_list[0];
				} else {
					t0.Data_ID = DSI_GERNERIC_SHORT_PACKET_ID_1;
					t0.Data1 = 0;
				}
				OUTREG32(&DSI_CMDQ_REG->data[0], AS_UINT32(&t0));
				OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);
			}
		}

		/* for (i = 0; i < AS_UINT32(&DSI_REG->DSI_CMDQ_SIZE); i++) */
		/*ISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI",
		   "DSI_set_cmdq_V2. DSI_CMDQ+%04x : 0x%08x\n", i*4, DISP_REG_GET(DDP_REG_BASE_DSI + 0x180 + i*4)); */

		if (force_update) {
			MMProfileLogEx(MTKFB_MMP_Events.DSICmd, MMProfileFlagStart,
				       *(unsigned int *)(&DSI_CMDQ_REG->data[0]),
				       *(unsigned int *)(&DSI_CMDQ_REG->data[1]));
			DSI_Start();
			for (i = 0; i < 10; i++)
				;
			_WaitForEngineNotBusy();
			MMProfileLogEx(MTKFB_MMP_Events.DSICmd, MMProfileFlagEnd,
				       *(unsigned int *)(&DSI_CMDQ_REG->data[2]),
				       *(unsigned int *)(&DSI_CMDQ_REG->data[3]));
		}
	}

}

void DSI_set_cmdq_V3(LCM_setting_table_V3 *para_tbl, unsigned int size, unsigned char force_update)
{
	uint32_t i;
	uint32_t goto_addr, mask_para, set_para;
	/* uint32_t fbPhysAddr, fbVirAddr; */
	DSI_T0_INS t0;
	/* DSI_T1_INS t1; */
	DSI_T2_INS t2;

	uint32_t index = 0;

	unsigned char data_id, cmd, count;
	unsigned char *para_list;

	do {
		data_id = para_tbl[index].id;
		cmd = para_tbl[index].cmd;
		count = para_tbl[index].count;
		para_list = para_tbl[index].para_list;

		if (data_id == REGFLAG_ESCAPE_ID && cmd == REGFLAG_DELAY_MS_V3) {
			udelay(1000 * count);
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI",
				       "DSI_set_cmdq_V3[%d]. Delay %d (ms)\n", index, count);

			continue;
		}

		if (0 != DSI_REG->DSI_MODE_CTRL.MODE) {	/* not in cmd mode */
			DSI_VM_CMD_CON_REG vm_cmdq;

			OUTREG32(&vm_cmdq, AS_UINT32(&DSI_REG->DSI_VM_CMD_CON));
			pr_info("set cmdq in VDO mode\n");
			if (count > 1) {
				vm_cmdq.LONG_PKT = 1;
				vm_cmdq.CM_DATA_ID = data_id;
				vm_cmdq.CM_DATA_0 = count + 1;
				OUTREG32(&DSI_REG->DSI_VM_CMD_CON, AS_UINT32(&vm_cmdq));

				goto_addr = (uint32_t) (&DSI_VM_CMD_REG->data[0].byte0);
				mask_para = (0xFF << ((goto_addr & 0x3) * 8));
				set_para = (cmd << ((goto_addr & 0x3) * 8));
				MASKREG32(goto_addr & (~0x3), mask_para, set_para);

				for (i = 0; i < count; i++) {
					goto_addr = (uint32_t) (&DSI_VM_CMD_REG->data[0].byte1) + i;
					mask_para = (0xFF << ((goto_addr & 0x3) * 8));
					set_para = (para_list[i] << ((goto_addr & 0x3) * 8));
					MASKREG32(goto_addr & (~0x3), mask_para, set_para);
				}
			} else {
				vm_cmdq.LONG_PKT = 0;
				vm_cmdq.CM_DATA_0 = cmd;
				if (count) {
					vm_cmdq.CM_DATA_ID = data_id;
					vm_cmdq.CM_DATA_1 = para_list[0];
				} else {
					vm_cmdq.CM_DATA_ID = data_id;
					vm_cmdq.CM_DATA_1 = 0;
				}
				OUTREG32(&DSI_REG->DSI_VM_CMD_CON, AS_UINT32(&vm_cmdq));
			}
			/* start DSI VM CMDQ */
			if (force_update) {
				MMProfileLogEx(MTKFB_MMP_Events.DSICmd, MMProfileFlagStart,
					       *(unsigned int *)(&DSI_VM_CMD_REG->data[0]),
					       *(unsigned int *)(&DSI_VM_CMD_REG->data[1]));
				DSI_EnableVM_CMD();

				/* must wait VM CMD done? */
				MMProfileLogEx(MTKFB_MMP_Events.DSICmd, MMProfileFlagEnd,
					       *(unsigned int *)(&DSI_VM_CMD_REG->data[2]),
					       *(unsigned int *)(&DSI_VM_CMD_REG->data[3]));
			}
		} else {
			_WaitForEngineNotBusy();
			{
				/* for(i = 0; i < sizeof(DSI_CMDQ_REG->data0) / sizeof(DSI_CMDQ); i++) */
				/* OUTREG32(&DSI_CMDQ_REG->data0[i], 0); */
				/* memset(&DSI_CMDQ_REG->data[0], 0, sizeof(DSI_CMDQ_REG->data[0])); */
				OUTREG32(&DSI_CMDQ_REG->data[0], 0);

				if (count > 1) {
					t2.CONFG = 2;
					t2.Data_ID = data_id;
					t2.WC16 = count + 1;

					OUTREG32(&DSI_CMDQ_REG->data[0].byte0, AS_UINT32(&t2));

					goto_addr = (uint32_t) (&DSI_CMDQ_REG->data[1].byte0);
					mask_para = (0xFF << ((goto_addr & 0x3) * 8));
					set_para = (cmd << ((goto_addr & 0x3) * 8));
					MASKREG32(goto_addr & (~0x3), mask_para, set_para);

					for (i = 0; i < count; i++) {
						goto_addr =
						    (uint32_t) (&DSI_CMDQ_REG->data[1].byte1) + i;
						mask_para = (0xFF << ((goto_addr & 0x3) * 8));
						set_para =
						    (para_list[i] << ((goto_addr & 0x3) * 8));
						MASKREG32(goto_addr & (~0x3), mask_para, set_para);
					}

					OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 2 + (count) / 4);
				} else {
					t0.CONFG = 0;
					t0.Data0 = cmd;
					if (count) {
						t0.Data_ID = data_id;
						t0.Data1 = para_list[0];
					} else {
						t0.Data_ID = data_id;
						t0.Data1 = 0;
					}
					OUTREG32(&DSI_CMDQ_REG->data[0], AS_UINT32(&t0));
					OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);
				}

				for (i = 0; i < AS_UINT32(&DSI_REG->DSI_CMDQ_SIZE); i++)
					DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI",
						       "DSI_set_cmdq_V3[%d]. DSI_CMDQ+%04x : 0x%08x\n",
						       index, i * 4,
						       DISP_REG_GET(DDP_REG_BASE_DSI + 0x180 +
								    i * 4));

				if (force_update) {
					MMProfileLog(MTKFB_MMP_Events.DSICmd, MMProfileFlagStart);
					DSI_Start();
					for (i = 0; i < 10; i++)
						;
					_WaitForEngineNotBusy();
					MMProfileLog(MTKFB_MMP_Events.DSICmd, MMProfileFlagEnd);
				}
			}
		}
	} while (++index < size);

}

void DSI_set_cmdq(unsigned int *pdata, unsigned int queue_size, unsigned char force_update)
{
	uint32_t i;

/* _WaitForEngineNotBusy(); */

	if (0 != DSI_REG->DSI_MODE_CTRL.MODE) {	/* not in cmd mode */
		DSI_VM_CMD_CON_REG vm_cmdq;

		OUTREG32(&vm_cmdq, AS_UINT32(&DSI_REG->DSI_VM_CMD_CON));
		pr_info("set cmdq in VDO mode\n");
		if (queue_size > 1) {	/* long packet */
			unsigned int i = 0;

			vm_cmdq.LONG_PKT = 1;
			vm_cmdq.CM_DATA_ID = ((pdata[0] >> 8) & 0xFF);
			vm_cmdq.CM_DATA_0 = ((pdata[0] >> 16) & 0xFF);
			vm_cmdq.CM_DATA_1 = 0;
			OUTREG32(&DSI_REG->DSI_VM_CMD_CON, AS_UINT32(&vm_cmdq));
			for (i = 0; i < queue_size - 1; i++)
				OUTREG32(&DSI_VM_CMD_REG->data[i], AS_UINT32((pdata + i + 1)));
		} else {
			vm_cmdq.LONG_PKT = 0;
			vm_cmdq.CM_DATA_ID = ((pdata[0] >> 8) & 0xFF);
			vm_cmdq.CM_DATA_0 = ((pdata[0] >> 16) & 0xFF);
			vm_cmdq.CM_DATA_1 = ((pdata[0] >> 24) & 0xFF);
			OUTREG32(&DSI_REG->DSI_VM_CMD_CON, AS_UINT32(&vm_cmdq));
		}
		/* start DSI VM CMDQ */
		if (force_update) {
			MMProfileLogEx(MTKFB_MMP_Events.DSICmd, MMProfileFlagStart,
				       *(unsigned int *)(&DSI_VM_CMD_REG->data[0]),
				       *(unsigned int *)(&DSI_VM_CMD_REG->data[1]));
			DSI_EnableVM_CMD();

			/* must wait VM CMD done? */
			MMProfileLogEx(MTKFB_MMP_Events.DSICmd, MMProfileFlagEnd,
				       *(unsigned int *)(&DSI_VM_CMD_REG->data[2]),
				       *(unsigned int *)(&DSI_VM_CMD_REG->data[3]));
		}
	} else {
		ASSERT(queue_size <= 32);
		_WaitForEngineNotBusy();
#ifdef ENABLE_DSI_ERROR_REPORT
		if ((pdata[0] & 1)) {
			memcpy(_dsi_cmd_queue, pdata, queue_size * 4);
			_dsi_cmd_queue[queue_size++] = 0x4;
			pdata = (unsigned int *)_dsi_cmd_queue;
		} else {
			pdata[0] |= 4;
		}
#endif

		for (i = 0; i < queue_size; i++)
			OUTREG32(&DSI_CMDQ_REG->data[i], AS_UINT32((pdata + i)));

		OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, queue_size);

		/* for (i = 0; i < queue_size; i++) */
		/* pr_info("[DISP] - kernel - DSI_set_cmdq. DSI_CMDQ+%04x : 0x%08x\n",
		   i*4, DISP_REG_GET(DDP_REG_BASE_DSI + 0x180 + i*4)); */

		if (force_update) {
			MMProfileLogEx(MTKFB_MMP_Events.DSICmd, MMProfileFlagStart,
				       *(unsigned int *)(&DSI_CMDQ_REG->data[0]),
				       *(unsigned int *)(&DSI_CMDQ_REG->data[1]));
			DSI_Start();
			for (i = 0; i < 10; i++)
				;
			_WaitForEngineNotBusy();
			MMProfileLogEx(MTKFB_MMP_Events.DSICmd, MMProfileFlagEnd,
				       *(unsigned int *)(&DSI_CMDQ_REG->data[2]),
				       *(unsigned int *)(&DSI_CMDQ_REG->data[3]));
		}
	}
}


DSI_STATUS DSI_Write_T0_INS(DSI_T0_INS *t0)
{
	OUTREG32(&DSI_CMDQ_REG->data[0], AS_UINT32(t0));

	OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);
	OUTREG32(&DSI_REG->DSI_START, 0);
	OUTREG32(&DSI_REG->DSI_START, 1);

	return DSI_STATUS_OK;
}


DSI_STATUS DSI_Write_T1_INS(DSI_T1_INS *t1)
{
	OUTREG32(&DSI_CMDQ_REG->data[0], AS_UINT32(t1));

	OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);
	OUTREG32(&DSI_REG->DSI_START, 0);
	OUTREG32(&DSI_REG->DSI_START, 1);

	return DSI_STATUS_OK;
}


DSI_STATUS DSI_Write_T2_INS(DSI_T2_INS *t2)
{
	unsigned int i;

	OUTREG32(&DSI_CMDQ_REG->data[0], AS_UINT32(t2));

	for (i = 0; i < ((t2->WC16 - 1) >> 2) + 1; i++)
		OUTREG32(&DSI_CMDQ_REG->data[1 + i], AS_UINT32((t2->pdata + i)));

	OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, (((t2->WC16 - 1) >> 2) + 2));
	OUTREG32(&DSI_REG->DSI_START, 0);
	OUTREG32(&DSI_REG->DSI_START, 1);

	return DSI_STATUS_OK;
}


DSI_STATUS DSI_Write_T3_INS(DSI_T3_INS *t3)
{
	OUTREG32(&DSI_CMDQ_REG->data[0], AS_UINT32(t3));

	OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);
	OUTREG32(&DSI_REG->DSI_START, 0);
	OUTREG32(&DSI_REG->DSI_START, 1);

	return DSI_STATUS_OK;
}

DSI_STATUS DSI_TXRX_Control(bool cksm_en,
			    bool ecc_en,
			    unsigned char lane_num,
			    unsigned char vc_num,
			    bool null_packet_en,
			    bool err_correction_en,
			    bool dis_eotp_en, bool hstx_cklp_en, unsigned int max_return_size)
{
	DSI_TXRX_CTRL_REG tmp_reg;

	tmp_reg = DSI_REG->DSI_TXRX_CTRL;

	switch (lane_num) {
	case LCM_ONE_LANE:
		tmp_reg.LANE_NUM = 1;
		break;
	case LCM_TWO_LANE:
		tmp_reg.LANE_NUM = 3;
		break;
	case LCM_THREE_LANE:
		tmp_reg.LANE_NUM = 0x7;
		break;
	case LCM_FOUR_LANE:
		tmp_reg.LANE_NUM = 0xF;
		break;
	}
	tmp_reg.VC_NUM = vc_num;
	tmp_reg.DIS_EOT = dis_eotp_en;
	tmp_reg.NULL_EN = null_packet_en;
	tmp_reg.MAX_RTN_SIZE = max_return_size;
	tmp_reg.HSTX_CKLP_EN = hstx_cklp_en;
	OUTREG32(&DSI_REG->DSI_TXRX_CTRL, AS_UINT32(&tmp_reg));

	return DSI_STATUS_OK;
}


DSI_STATUS DSI_PS_Control(unsigned int ps_type, unsigned int vact_line, unsigned int ps_wc)
{
	DSI_PSCTRL_REG tmp_reg;
	uint32_t tmp_hstx_cklp_wc;

	tmp_reg = DSI_REG->DSI_PSCTRL;

	/* /TODO: parameter checking */
	ASSERT(ps_type <= PACKED_PS_18BIT_RGB666);
	if (ps_type > LOOSELY_PS_18BIT_RGB666)
		tmp_reg.DSI_PS_SEL = (5 - ps_type);
	else
		tmp_reg.DSI_PS_SEL = ps_type;
	tmp_reg.DSI_PS_WC = ps_wc;
	tmp_hstx_cklp_wc = ps_wc;

	OUTREG32(&DSI_REG->DSI_VACT_NL, AS_UINT32(&vact_line));
	OUTREG32(&DSI_REG->DSI_PSCTRL, AS_UINT32(&tmp_reg));
	OUTREG32(&DSI_REG->DSI_HSTX_CKL_WC, tmp_hstx_cklp_wc);
	return DSI_STATUS_OK;
}

/* unsigned int dsi_cycle_time; */

void DSI_Config_VDO_Timing(LCM_PARAMS *lcm_params)
{
	unsigned int line_byte;
	unsigned int horizontal_sync_active_byte = 0;
	unsigned int horizontal_backporch_byte;
	unsigned int horizontal_frontporch_byte;
	unsigned int horizontal_bllp_byte;
	unsigned int dsiTmpBufBpp;

#define LINE_PERIOD_US              (8 * line_byte * _dsiContext.bit_time_ns / 1000)

	if (lcm_params->dsi.data_format.format == LCM_DSI_FORMAT_RGB565)
		dsiTmpBufBpp = 2;
	else
		dsiTmpBufBpp = 3;

	OUTREG32(&DSI_REG->DSI_VSA_NL, lcm_params->dsi.vertical_sync_active);
	OUTREG32(&DSI_REG->DSI_VBP_NL, lcm_params->dsi.vertical_backporch);
	OUTREG32(&DSI_REG->DSI_VFP_NL, lcm_params->dsi.vertical_frontporch);
	OUTREG32(&DSI_REG->DSI_VACT_NL, lcm_params->dsi.vertical_active_line);

	line_byte = (lcm_params->dsi.horizontal_sync_active
		     + lcm_params->dsi.horizontal_backporch
		     + lcm_params->dsi.horizontal_frontporch
		     + lcm_params->dsi.horizontal_active_pixel) * dsiTmpBufBpp;

	if (lcm_params->dsi.mode == SYNC_EVENT_VDO_MODE || lcm_params->dsi.mode == BURST_VDO_MODE) {
		ASSERT((lcm_params->dsi.horizontal_backporch +
			lcm_params->dsi.horizontal_sync_active) * dsiTmpBufBpp > 9);
		horizontal_backporch_byte =
		    ((lcm_params->dsi.horizontal_backporch +
		      lcm_params->dsi.horizontal_sync_active) * dsiTmpBufBpp - 10);
	} else {
		ASSERT(lcm_params->dsi.horizontal_sync_active * dsiTmpBufBpp > 9);
		horizontal_sync_active_byte =
		    (lcm_params->dsi.horizontal_sync_active * dsiTmpBufBpp - 10);

		ASSERT(lcm_params->dsi.horizontal_backporch * dsiTmpBufBpp > 9);
		horizontal_backporch_byte =
		    (lcm_params->dsi.horizontal_backporch * dsiTmpBufBpp - 10);
	}

	ASSERT(lcm_params->dsi.horizontal_frontporch * dsiTmpBufBpp > 11);
	horizontal_frontporch_byte = (lcm_params->dsi.horizontal_frontporch * dsiTmpBufBpp - 12);
	horizontal_bllp_byte = (lcm_params->dsi.horizontal_bllp * dsiTmpBufBpp);
/* ASSERT(lcm_params->dsi.horizontal_frontporch * dsiTmpBufBpp > ((300/dsi_cycle_time) * lcm_params->dsi.LANE_NUM)); */
/* horizontal_frontporch_byte -= ((300/dsi_cycle_time) * lcm_params->dsi.LANE_NUM); */

	OUTREG32(&DSI_REG->DSI_HSA_WC, ALIGN_TO((horizontal_sync_active_byte), 4));
	OUTREG32(&DSI_REG->DSI_HBP_WC, ALIGN_TO((horizontal_backporch_byte), 4));
	OUTREG32(&DSI_REG->DSI_HFP_WC, ALIGN_TO((horizontal_frontporch_byte), 4));
	OUTREG32(&DSI_REG->DSI_BLLP_WC, ALIGN_TO((horizontal_bllp_byte), 4));

	_dsiContext.vfp_period_us = LINE_PERIOD_US * lcm_params->dsi.vertical_frontporch / 1000;
	_dsiContext.vsa_vs_period_us = LINE_PERIOD_US * 1 / 1000;
	_dsiContext.vsa_hs_period_us =
	    LINE_PERIOD_US * (lcm_params->dsi.vertical_sync_active - 2) / 1000;
	_dsiContext.vsa_ve_period_us = LINE_PERIOD_US * 1 / 1000;
	_dsiContext.vbp_period_us = LINE_PERIOD_US * lcm_params->dsi.vertical_backporch / 1000;

	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "video timing, mode = %d\n", lcm_params->dsi.mode);
	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "VSA : %d %d(us)\n",
		       *(unsigned int *)&DSI_REG->DSI_VSA_NL,
		       (_dsiContext.vsa_vs_period_us + _dsiContext.vsa_hs_period_us +
			_dsiContext.vsa_ve_period_us));
	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "VBP : %d %d(us)\n",
		       *(unsigned int *)&DSI_REG->DSI_VBP_NL, _dsiContext.vbp_period_us);
	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "VFP : %d %d(us)\n",
		       *(unsigned int *)&DSI_REG->DSI_VFP_NL, _dsiContext.vfp_period_us);
	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "VACT: %d\n",
		       *(unsigned int *)&DSI_REG->DSI_VACT_NL);
}

void DSI_write_lcm_cmd(unsigned int cmd)
{
	DSI_T0_INS t0_tmp;
	DSI_CMDQ_CONFG CONFG_tmp;

	CONFG_tmp.type = SHORT_PACKET_RW;
	CONFG_tmp.BTA = DISABLE_BTA;
	CONFG_tmp.HS = LOW_POWER;
	CONFG_tmp.CL = CL_8BITS;
	CONFG_tmp.TE = DISABLE_TE;
	CONFG_tmp.RPT = DISABLE_RPT;

	t0_tmp.CONFG = *((unsigned char *)(&CONFG_tmp));
	t0_tmp.Data_ID = (cmd & 0xFF);
	t0_tmp.Data0 = 0x0;
	t0_tmp.Data1 = 0x0;

	DSI_Write_T0_INS(&t0_tmp);
}


void DSI_write_lcm_regs(unsigned int addr, unsigned int *para, unsigned int nums)
{
	DSI_T2_INS *t2_tmp = 0;
	DSI_CMDQ_CONFG CONFG_tmp;

	CONFG_tmp.type = LONG_PACKET_W;
	CONFG_tmp.BTA = DISABLE_BTA;
	CONFG_tmp.HS = LOW_POWER;
	CONFG_tmp.CL = CL_8BITS;
	CONFG_tmp.TE = DISABLE_TE;
	CONFG_tmp.RPT = DISABLE_RPT;

	t2_tmp->CONFG = *((unsigned char *)(&CONFG_tmp));
	t2_tmp->Data_ID = (addr & 0xFF);
	t2_tmp->WC16 = nums;
	t2_tmp->pdata = para;

	DSI_Write_T2_INS(t2_tmp);

}

uint32_t DSI_dcs_read_lcm_reg(uint8_t cmd)
{
	/* uint32_t max_try_count = 5; */
	uint32_t recv_data = 0;
	/* uint32_t recv_data_cnt; */
	/* unsigned int read_timeout_ms; */
	/* unsigned char packet_type; */
#if 0
	DSI_T0_INS t0;
#if ENABLE_DSI_INTERRUPT
	static const long WAIT_TIMEOUT = 2 * HZ;	/* 2 sec */
	long ret;
#endif

	if (DSI_REG->DSI_MODE_CTRL.MODE)
		return 0;

	do {
		if (max_try_count == 0)
			return 0;

		max_try_count--;
		recv_data = 0;
		recv_data_cnt = 0;
		read_timeout_ms = 20;

		_WaitForEngineNotBusy();

		t0.CONFG = 0x04;	/* /BTA */
		t0.Data0 = cmd;
		t0.Data_ID = DSI_DCS_READ_PACKET_ID;
		t0.Data1 = 0;

		OUTREG32(&DSI_CMDQ_REG->data[0], AS_UINT32(&t0));
		OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);

		/* /clear read ACK */
		DSI_REG->DSI_RACK.DSI_RACK = 1;
		DSI_REG->DSI_INTSTA.RD_RDY = 1;
		DSI_REG->DSI_INTSTA.CMD_DONE = 1;
		DSI_REG->DSI_INTEN.RD_RDY = 1;
		DSI_REG->DSI_INTEN.CMD_DONE = 1;

		OUTREG32(&DSI_REG->DSI_START, 0);
		OUTREG32(&DSI_REG->DSI_START, 1);

		/* / the following code is to */
		/* / 1: wait read ready */
		/* / 2: ack read ready */
		/* / 3: wait for CMDQ_DONE */
		/* / 3: read data */
#if ENABLE_DSI_INTERRUPT
		ret = wait_event_interruptible_timeout(_dsi_dcs_read_wait_queue,
						       !_IsEngineBusy(), WAIT_TIMEOUT);
		if (0 == ret) {
			DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
				       " Wait for DSI engine read ready timeout!!!\n");

			DSI_DumpRegisters();

			/* /do necessary reset here */
			DSI_REG->DSI_RACK.DSI_RACK = 1;
			DSI_Reset();

			return 0;
		}
#else
#ifdef DSI_DRV_DEBUG_LOG_ENABLE
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " Start polling DSI read ready!!!\n");
#endif
		while (DSI_REG->DSI_INTSTA.RD_RDY == 0) {
			/* /keep polling */
			msleep(20);
			read_timeout_ms--;

			if (read_timeout_ms == 0) {
				DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI",
					       " Polling DSI read ready timeout!!!\n");
				DSI_DumpRegisters();

				/* /do necessary reset here */
				DSI_REG->DSI_RACK.DSI_RACK = 1;
				DSI_Reset();
				return 0;
			}
		}
#ifdef DSI_DRV_DEBUG_LOG_ENABLE
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " End polling DSI read ready!!!\n");
#endif

		DSI_REG->DSI_RACK.DSI_RACK = 1;

		while (DSI_REG->DSI_STA.BUF_UNDERRUN || DSI_REG->DSI_STA.ESC_ENTRY_ERR
		       || DSI_REG->DSI_STA.LPDT_SYNC_ERR || DSI_REG->DSI_STA.CTRL_ERR
		       || DSI_REG->DSI_STA.CONTENT_ERR) {
			/* /DSI READ ACK HW bug workaround */
#ifdef DSI_DRV_DEBUG_LOG_ENABLE
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "DSI is busy: 0x%x !!!\n",
				       DSI_REG->DSI_STA.BUSY);
#endif
			DSI_REG->DSI_RACK.DSI_RACK = 1;
		}


		/* /clear interrupt status */
		DSI_REG->DSI_INTSTA.RD_RDY = 1;
		/* /STOP DSI */
		OUTREG32(&DSI_REG->DSI_START, 0);

#endif

		DSI_REG->DSI_INTEN.RD_RDY = 0;

#ifdef DSI_DRV_DEBUG_LOG_ENABLE
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_STA : 0x%x\n",
			       DSI_REG->DSI_RX_STA);
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_CMDQ_SIZE : 0x%x\n",
			       DSI_REG->DSI_CMDQ_SIZE.CMDQ_SIZE);
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_CMDQ_DATA0 : 0x%x\n",
			       DSI_CMDQ_REG->data[0].byte0);
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_CMDQ_DATA1 : 0x%x\n",
			       DSI_CMDQ_REG->data[0].byte1);
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_CMDQ_DATA2 : 0x%x\n",
			       DSI_CMDQ_REG->data[0].byte2);
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_CMDQ_DATA3 : 0x%x\n",
			       DSI_CMDQ_REG->data[0].byte3);
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_DATA.BYTE0 : 0x%x\n",
			       DSI_REG->DSI_RX_DATA.BYTE0);
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_DATA.BYTE1 : 0x%x\n",
			       DSI_REG->DSI_RX_DATA.BYTE1);
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_DATA.BYTE2 : 0x%x\n",
			       DSI_REG->DSI_RX_DATA.BYTE2);
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_DATA.BYTE3 : 0x%x\n",
			       DSI_REG->DSI_RX_DATA.BYTE3);
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_DATA.BYTE4 : 0x%x\n",
			       DSI_REG->DSI_RX_DATA.BYTE4);
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_DATA.BYTE5 : 0x%x\n",
			       DSI_REG->DSI_RX_DATA.BYTE5);
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_DATA.BYTE6 : 0x%x\n",
			       DSI_REG->DSI_RX_DATA.BYTE6);
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_DATA.BYTE7 : 0x%x\n",
			       DSI_REG->DSI_RX_DATA.BYTE7);
#endif
		packet_type = DSI_REG->DSI_RX_DATA.BYTE0;

#ifdef DSI_DRV_DEBUG_LOG_ENABLE
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI read packet_type is 0x%x\n",
			       packet_type);
#endif
		if (DSI_REG->DSI_RX_STA.LONG == 1) {
			recv_data_cnt =
			    DSI_REG->DSI_RX_DATA.BYTE1 + DSI_REG->DSI_RX_DATA.BYTE2 * 16;
			if (recv_data_cnt > 4) {
#ifdef DSI_DRV_DEBUG_LOG_ENABLE
				DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
					       " DSI read long packet data  exceeds 4 bytes\n");
#endif
				recv_data_cnt = 4;
			}
			memcpy((void *)&recv_data, (void *)&DSI_REG->DSI_RX_DATA.BYTE4,
			       recv_data_cnt);
		} else {
			memcpy((void *)&recv_data, (void *)&DSI_REG->DSI_RX_DATA.BYTE1, 2);
		}

#ifdef DSI_DRV_DEBUG_LOG_ENABLE
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI read 0x%x data is 0x%x\n", cmd,
			       recv_data);
#endif
	} while (packet_type != 0x1C && packet_type != 0x21 && packet_type != 0x22);
	/* / here: we may receive a ACK packet which packet type is 0x02 (incdicates some error happened) */
	/* / therefore we try re-read again until no ACK packet */
	/* / But: if it is a good way to keep re-trying ??? */
#endif
	return recv_data;
}

/* / return value: the data length we got */
uint32_t DSI_dcs_read_lcm_reg_v2(uint8_t cmd, uint8_t *buffer, uint8_t buffer_size)
{
	uint32_t max_try_count = 5;
	uint32_t recv_data_cnt;
	unsigned int read_timeout_ms;
	unsigned char packet_type;
	DSI_RX_DATA_REG read_data0;
	DSI_RX_DATA_REG read_data1;
	DSI_RX_DATA_REG read_data2;
	DSI_RX_DATA_REG read_data3;
#if 1
	DSI_T0_INS t0;

#if ENABLE_DSI_INTERRUPT
	static const long WAIT_TIMEOUT = HZ / 2;	/* 2 sec//Yifan Modified for ESD Check with out LCM */
	long ret;
#endif
	if (DSI_REG->DSI_MODE_CTRL.MODE)
		return 0;

	if (buffer == NULL || buffer_size == 0)
		return 0;

	do {
		if (max_try_count == 0)
			return 0;
		max_try_count--;
		recv_data_cnt = 0;
		read_timeout_ms = 20;

		_WaitForEngineNotBusy();

		t0.CONFG = 0x04;	/* /BTA */
		t0.Data0 = cmd;
		if (buffer_size < 0x3)
			t0.Data_ID = DSI_DCS_READ_PACKET_ID;
		else
			t0.Data_ID = DSI_GERNERIC_READ_LONG_PACKET_ID;
		t0.Data1 = 0;

		OUTREG32(&DSI_CMDQ_REG->data[0], AS_UINT32(&t0));
		OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);

		/* /clear read ACK */
		/* DSI_REG->DSI_RACK.DSI_RACK = 1; */
		/* DSI_REG->DSI_INTSTA.RD_RDY = 1; */
		/* DSI_REG->DSI_INTSTA.CMD_DONE = 1; */
		/* DSI_REG->DSI_INTEN.RD_RDY =  1; */
		/* DSI_REG->DSI_INTEN.CMD_DONE=  1; */
		OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);
		OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, RD_RDY, 1);
		OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, CMD_DONE, 1);
		OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, RD_RDY, 1);
		OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, CMD_DONE, 1);



		OUTREG32(&DSI_REG->DSI_START, 0);
		OUTREG32(&DSI_REG->DSI_START, 1);

		/* / the following code is to */
		/* / 1: wait read ready */
		/* / 2: ack read ready */
		/* / 3: wait for CMDQ_DONE */
		/* / 3: read data */
#if ENABLE_DSI_INTERRUPT
		ret = wait_event_interruptible_timeout(_dsi_dcs_read_wait_queue,
						       !_IsEngineBusy(), WAIT_TIMEOUT);
		if (0 == ret) {
			DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
				       " Wait for DSI engine read ready timeout!!!\n");

			DSI_DumpRegisters();

			/* /do necessary reset here */
			/* DSI_REG->DSI_RACK.DSI_RACK = 1; */
			OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);
			DSI_Reset();

			return 0;
		}
#else
#ifdef DDI_DRV_DEBUG_LOG_ENABLE
		if (dsi_log_on)
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI",
				       " Start polling DSI read ready!!!\n");
#endif
		while (DSI_REG->DSI_INTSTA.RD_RDY == 0) {
			/* /keep polling */
			msleep(20);
			read_timeout_ms--;

			if (read_timeout_ms == 0) {
				DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI",
					       " Polling DSI read ready timeout!!!\n");
				DSI_DumpRegisters();

				/* /do necessary reset here */
				/* DSI_REG->DSI_RACK.DSI_RACK = 1; */
				OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);
				DSI_Reset();
				return 0;
			}
		}
#ifdef DDI_DRV_DEBUG_LOG_ENABLE
		if (dsi_log_on)
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " End polling DSI read ready!!!\n");
#endif

		/* DSI_REG->DSI_RACK.DSI_RACK = 1; */
		OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);

		/* /clear interrupt status */
		/* DSI_REG->DSI_INTSTA.RD_RDY = 1; */
		OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, RD_RDY, 1);
		/* /STOP DSI */
		OUTREG32(&DSI_REG->DSI_START, 0);

#endif

		/* DSI_REG->DSI_INTEN.RD_RDY =  0; */
		OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, RD_RDY, 1);

		OUTREG32(&read_data0, AS_UINT32(&DSI_REG->DSI_RX_DATA0));
		OUTREG32(&read_data1, AS_UINT32(&DSI_REG->DSI_RX_DATA1));
		OUTREG32(&read_data2, AS_UINT32(&DSI_REG->DSI_RX_DATA2));
		OUTREG32(&read_data3, AS_UINT32(&DSI_REG->DSI_RX_DATA3));
#ifdef DDI_DRV_DEBUG_LOG_ENABLE
		if (dsi_log_on) {
			unsigned int i;
/* DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_STA : 0x%x\n", DSI_REG->DSI_RX_STA); */
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_CMDQ_SIZE : 0x%x\n",
				       DSI_REG->DSI_CMDQ_SIZE.CMDQ_SIZE);
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_CMDQ_DATA0 : 0x%x\n",
				       DSI_CMDQ_REG->data[0].byte0);
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_CMDQ_DATA1 : 0x%x\n",
				       DSI_CMDQ_REG->data[0].byte1);
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_CMDQ_DATA2 : 0x%x\n",
				       DSI_CMDQ_REG->data[0].byte2);
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_CMDQ_DATA3 : 0x%x\n",
				       DSI_CMDQ_REG->data[0].byte3);
#if 1
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_DATA0 : 0x%x\n",
				       DSI_REG->DSI_RX_DATA0);
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_DATA1 : 0x%x\n",
				       DSI_REG->DSI_RX_DATA1);
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_DATA2 : 0x%x\n",
				       DSI_REG->DSI_RX_DATA2);
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_DATA3 : 0x%x\n",
				       DSI_REG->DSI_RX_DATA3);

			pr_info("read_data0, %x,%x,%x,%x\n", read_data0.byte0, read_data0.byte1,
				read_data0.byte2, read_data0.byte3);
			pr_info("read_data1, %x,%x,%x,%x\n", read_data1.byte0, read_data1.byte1,
				read_data1.byte2, read_data1.byte3);
			pr_info("read_data2, %x,%x,%x,%x\n", read_data2.byte0, read_data2.byte1,
				read_data2.byte2, read_data2.byte3);
			pr_info("read_data3, %x,%x,%x,%x\n", read_data3.byte0, read_data3.byte1,
				read_data3.byte2, read_data3.byte3);
#endif
		}
#endif

#if 1
		packet_type = read_data0.byte0;

#ifdef DDI_DRV_DEBUG_LOG_ENABLE
		if (dsi_log_on)
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI read packet_type is 0x%x\n",
				       packet_type);
#endif



		if (packet_type == 0x1A || packet_type == 0x1C) {
			recv_data_cnt = read_data0.byte1 + read_data0.byte2 * 16;
			if (recv_data_cnt > 10) {
#ifdef DDI_DRV_DEBUG_LOG_ENABLE
				if (dsi_log_on)
					DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
						       " DSI read long packet data  exceeds 4 bytes\n");
#endif
				recv_data_cnt = 10;
			}

			if (recv_data_cnt > buffer_size) {
#ifdef DDI_DRV_DEBUG_LOG_ENABLE
				if (dsi_log_on)
					DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
						       " DSI read long packet data  exceeds buffer size: %d\n",
						       buffer_size);
#endif
				recv_data_cnt = buffer_size;
			}
#ifdef DDI_DRV_DEBUG_LOG_ENABLE
			if (dsi_log_on)
				DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
					       " DSI read long packet size: %d\n", recv_data_cnt);
#endif
			memcpy((void *)buffer, (void *)&read_data1, recv_data_cnt);
		} else {
			if (recv_data_cnt > buffer_size) {
#ifdef DDI_DRV_DEBUG_LOG_ENABLE
				if (dsi_log_on)
					DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
						       " DSI read short packet data  exceeds buffer size: %d\n",
						       buffer_size);
#endif
				recv_data_cnt = buffer_size;
			}
			memcpy((void *)buffer, (void *)&read_data0.byte1, 2);
		}
#endif
	} while (packet_type != 0x1C && packet_type != 0x21 && packet_type != 0x22
		 && packet_type != 0x1A);
	/* / here: we may receive a ACK packet which packet type is 0x02 (incdicates some error happened) */
	/* / therefore we try re-read again until no ACK packet */
	/* / But: if it is a good way to keep re-trying ??? */
#endif
	return recv_data_cnt;
}

uint32_t DSI_read_lcm_reg(void)
{
	return 0;
}


DSI_STATUS DSI_write_lcm_fb(unsigned int addr, bool long_length)
{
	DSI_T1_INS t1_tmp;
	DSI_CMDQ_CONFG CONFG_tmp;

	CONFG_tmp.type = FB_WRITE;
	CONFG_tmp.BTA = DISABLE_BTA;
	CONFG_tmp.HS = HIGH_SPEED;

	if (long_length)
		CONFG_tmp.CL = CL_16BITS;
	else
		CONFG_tmp.CL = CL_8BITS;

	CONFG_tmp.TE = DISABLE_TE;
	CONFG_tmp.RPT = DISABLE_RPT;


	t1_tmp.CONFG = *((unsigned char *)(&CONFG_tmp));
	t1_tmp.Data_ID = 0x39;
	t1_tmp.mem_start0 = (addr & 0xFF);

	if (long_length)
		t1_tmp.mem_start1 = ((addr >> 8) & 0xFF);

	return DSI_Write_T1_INS(&t1_tmp);


}


DSI_STATUS DSI_read_lcm_fb(unsigned char *buffer)
{
	/* unsigned int array[2]; */

	_WaitForEngineNotBusy();
#if 0
	array[0] = 0x000A3700;	/* read size */
	DSI_set_cmdq(array, 1, 1);

	DSI_dcs_read_lcm_reg_v2(0x2E, buffer, 10);
	DSI_dcs_read_lcm_reg_v2(0x2E, buffer + 10, 10);
	DSI_dcs_read_lcm_reg_v2(0x2E, buffer + 10 * 2, 10);
	DSI_dcs_read_lcm_reg_v2(0x2E, buffer + 10 * 3, 10);
	DSI_dcs_read_lcm_reg_v2(0x2E, buffer + 10 * 4, 10);
	DSI_dcs_read_lcm_reg_v2(0x2E, buffer + 10 * 5, 10);
#else
	/* if read_fb not impl, should return info */
	if (lcm_drv->read_fb)
		lcm_drv->read_fb(buffer);
#endif
	return DSI_STATUS_OK;
}

unsigned int DSI_Check_LCM(uint32_t color)
{
	unsigned int ret = 1;
	unsigned char buffer[60];
	unsigned int i = 0;

	OUTREG32(&DSI_REG->DSI_MEM_CONTI, DSI_RMEM_CONTI);
	DSI_read_lcm_fb(buffer);
	for (i = 0; i < 60; i++)
		pr_info("%d\n", buffer[i]);
	OUTREG32(&DSI_REG->DSI_MEM_CONTI, DSI_WMEM_CONTI);

	for (i = 0; i < 60; i += 3) {
		pr_info("read pixel = 0x%x,",
			(buffer[i] << 16) | (buffer[i + 1] << 8) | (buffer[i + 2]));
		if (((buffer[i] << 16) | (buffer[i + 1] << 8) | (buffer[i + 2])) !=
		    (color & 0xFFFFFF)) {
			ret = 0;
			break;
		}
	}
	return ret;
}

unsigned int DSI_BLS_Query(void)
{
	unsigned int ret;

	pr_info("BLS: 0x%x\n", DISP_REG_GET(0xF400A000));
	ret = (0x1 == (DISP_REG_GET(0xF400A000) & 0x1));
	return ret;		/* if 1, BLS enable */
}

void DSI_BLS_Enable(bool enable)
{
	if (enable) {
		OUTREG32(0xF400A0B0, 0x3);
		OUTREG32(0xF400A000, 0x00010001);
		OUTREG32(0xF400A0B0, 0x0);
	} else {
		OUTREG32(0xF400A0B0, 0x3);
		OUTREG32(0xF400A000, 0x00010000);
		OUTREG32(0xF400A0B0, 0x0);
	}
}

DSI_STATUS DSI_enable_MIPI_txio(bool en)
{
	return DSI_STATUS_OK;
}


bool Need_Wait_ULPS(void)
{
#ifndef MT65XX_NEW_DISP
	if (((DISP_REG_GET(DDP_REG_BASE_DSI + 0x14C) >> 24) & 0xFF) != 0x04) {

#ifdef DDI_DRV_DEBUG_LOG_ENABLE
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "[%s]:true\n", __func__);
#endif
		return true;

	} else {

#ifdef DDI_DRV_DEBUG_LOG_ENABLE
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "[%s]:false\n", __func__);
#endif
		return false;

	}
#else
	return false;
#endif
}


DSI_STATUS Wait_ULPS_Mode(void)
{
#ifndef MT65XX_NEW_DISP
	DSI_PHY_LCCON_REG lccon_reg = DSI_REG->DSI_PHY_LCCON;
	DSI_PHY_LD0CON_REG ld0con = DSI_REG->DSI_PHY_LD0CON;

	lccon_reg.LC_ULPM_EN = 1;
	ld0con.L0_ULPM_EN = 1;
	OUTREG32(&DSI_REG->DSI_PHY_LCCON, AS_UINT32(&lccon_reg));
	OUTREG32(&DSI_REG->DSI_PHY_LD0CON, AS_UINT32(&ld0con));

#ifdef DDI_DRV_DEBUG_LOG_ENABLE
	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "[%s]:enter\n", __func__);
#endif

	while (((DISP_REG_GET(DDP_REG_BASE_DSI + 0x14C) >> 24) & 0xFF) != 0x04) {
		lcm_mdelay(5);
#ifdef DDI_DRV_DEBUG_LOG_ENABLE
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "DSI+%04x : 0x%08x\n", DDP_REG_BASE_DSI,
			       DISP_REG_GET(DDP_REG_BASE_DSI + 0x14C));
#endif
	}

#ifdef DDI_DRV_DEBUG_LOG_ENABLE
	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "[%s]:exit\n", __func__);
#endif
#endif
	return DSI_STATUS_OK;

}


DSI_STATUS Wait_WakeUp(void)
{
#ifndef MT65XX_NEW_DISP
	DSI_PHY_LCCON_REG lccon_reg = DSI_REG->DSI_PHY_LCCON;
	DSI_PHY_LD0CON_REG ld0con = DSI_REG->DSI_PHY_LD0CON;

#ifdef DDI_DRV_DEBUG_LOG_ENABLE
	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "[%s]:enter\n", __func__);
#endif

	lccon_reg.LC_ULPM_EN = 0;
	ld0con.L0_ULPM_EN = 0;
	OUTREG32(&DSI_REG->DSI_PHY_LCCON, AS_UINT32(&lccon_reg));
	OUTREG32(&DSI_REG->DSI_PHY_LD0CON, AS_UINT32(&ld0con));

	lcm_mdelay(1);		/* Wait 1ms for LCM Spec */

	lccon_reg.LC_WAKEUP_EN = 1;
	ld0con.L0_WAKEUP_EN = 1;
	OUTREG32(&DSI_REG->DSI_PHY_LCCON, AS_UINT32(&lccon_reg));
	OUTREG32(&DSI_REG->DSI_PHY_LD0CON, AS_UINT32(&ld0con));

	while (((DISP_REG_GET(DDP_REG_BASE_DSI + 0x148) >> 8) & 0xFF) != 0x01) {
		lcm_mdelay(5);
#ifdef DDI_DRV_DEBUG_LOG_ENABLE
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "[soso]DSI+%04x : 0x%08x\n",
			       DDP_REG_BASE_DSI, DISP_REG_GET(DDP_REG_BASE_DSI + 0x148));
#endif
	}

#ifdef DDI_DRV_DEBUG_LOG_ENABLE
	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "[%s]:exit\n", __func__);
#endif

	lccon_reg.LC_WAKEUP_EN = 0;
	ld0con.L0_WAKEUP_EN = 0;
	OUTREG32(&DSI_REG->DSI_PHY_LCCON, AS_UINT32(&lccon_reg));
	OUTREG32(&DSI_REG->DSI_PHY_LD0CON, AS_UINT32(&ld0con));
#endif
	return DSI_STATUS_OK;

}

/* -------------------- Retrieve Information -------------------- */

DSI_STATUS DSI_DumpRegisters(void)
{
	uint32_t i;
	/*
	   description of dsi status
	   Bit Value   Description
	   [0] 0x0001  Idle (wait for command)
	   [1] 0x0002  Reading command queue for header
	   [2] 0x0004  Sending type-0 command
	   [3] 0x0008  Waiting frame data from RDMA for type-1 command
	   [4] 0x0010  Sending type-1 command
	   [5] 0x0020  Sending type-2 command
	   [6] 0x0040  Reading command queue for data
	   [7] 0x0080  Sending type-3 command
	   [8] 0x0100  Sending BTA
	   [9] 0x0200  Waiting RX-read data
	   [10]    0x0400  Waiting SW RACK for RX-read data
	   [11]    0x0800  Waiting TE
	   [12]    0x1000  Get TE
	   [13]    0x2000  Waiting external TE
	   [14]    0x4000  Waiting SW RACK for TE

	 */
	static const char *const DSI_DBG_STATUS_DESCRIPTION[] = {
		"null",
		"Idle (wait for command)",
		"Reading command queue for header",
		"Sending type-0 command",
		"Waiting frame data from RDMA for type-1 command",
		"Sending type-1 command",
		"Sending type-2 command",
		"Reading command queue for data",
		"Sending type-3 command",
		"Sending BTA",
		"Waiting RX-read data ",
		"Waiting SW RACK for RX-read data",
		"Waiting TE",
		"Get TE ",
		"Waiting external TE",
		"Waiting SW RACK for TE",
	};
	unsigned int DSI_DBG6_Status = (DISP_REG_GET(DDP_REG_BASE_DSI + 0x160)) & 0xffff;
	/* unsigned int DSI_DBG6_Status_bak = DSI_DBG6_Status; */
	int count = 0;

	while (DSI_DBG6_Status) {
		DSI_DBG6_Status >>= 1;
		count++;
	}
	/* while((1<<count) != DSI_DBG6_Status) count++; */
	/* count++; */
	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "---------- Start dump DSI registers ----------\n");
	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "DSI_STATE_DBG6=0x%08x, count=%d, means: [%s]\n",
		       DSI_DBG6_Status, count, DSI_DBG_STATUS_DESCRIPTION[count]);

	DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", "---------- Start dump DSI registers ----------\n");

	for (i = 0; i < sizeof(DSI_REGS); i += 16) {
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI",
			       "DSI+%04x : 0x%08x  0x%08x  0x%08x  0x%08x\n", i,
			       DISP_REG_GET(DDP_REG_BASE_DSI + i),
			       DISP_REG_GET(DDP_REG_BASE_DSI + i + 0x4),
			       DISP_REG_GET(DDP_REG_BASE_DSI + i + 0x8),
			       DISP_REG_GET(DDP_REG_BASE_DSI + i + 0xc));
	}

	for (i = 0; i < sizeof(DSI_CMDQ_REGS); i += 16) {
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI",
			       "DSI_CMD+%04x : 0x%08x  0x%08x  0x%08x  0x%08x\n", i,
			       DISP_REG_GET((DDP_REG_BASE_DSI + 0x180 + i)),
			       DISP_REG_GET((DDP_REG_BASE_DSI + 0x180 + i + 0x4)),
			       DISP_REG_GET((DDP_REG_BASE_DSI + 0x180 + i + 0x8)),
			       DISP_REG_GET((DDP_REG_BASE_DSI + 0x180 + i + 0xc)));
	}

	for (i = 0; i < sizeof(DSI_PHY_REGS); i += 16) {
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI",
			       "DSI_PHY+%04x : 0x%08x    0x%08x  0x%08x  0x%08x\n", i,
			       DISP_REG_GET((DDP_REG_BASE_MIPI + i)),
			       DISP_REG_GET((DDP_REG_BASE_MIPI + i + 0x4)),
			       DISP_REG_GET((DDP_REG_BASE_MIPI + i + 0x8)),
			       DISP_REG_GET((DDP_REG_BASE_MIPI + i + 0xc)));
	}

	return DSI_STATUS_OK;
}


static LCM_PARAMS lcm_params_for_clk_setting;


DSI_STATUS DSI_FMDesense_Query(void)
{
	return DSI_STATUS_OK;
}

DSI_STATUS DSI_FM_Desense(unsigned long freq)
{
	/* /need check */
	DSI_Change_CLK(freq);
	return DSI_STATUS_OK;
}

DSI_STATUS DSI_Reset_CLK(void)
{

	_WaitForEngineNotBusy();
	DSI_PHY_clk_setting(lcm_params);
	DSI_PHY_TIMCONFIG(lcm_params);
	return DSI_STATUS_OK;
}

DSI_STATUS DSI_Get_Default_CLK(unsigned int *clk)
{
	unsigned int div2_real = lcm_params->dsi.pll_div2 ? lcm_params->dsi.pll_div2 : 0x1;

	*clk = 13 * (lcm_params->dsi.pll_div1 + 1) / div2_real;
	return DSI_STATUS_OK;
}

DSI_STATUS DSI_Get_Current_CLK(unsigned int *clk)
{
#if 0
	if (mipitx_con1.RG_PLL_DIV2 == 0)
		*clk = 26 * (mipitx_con1.RG_PLL_DIV1 + 1);
	else
		*clk = 13 * (mipitx_con1.RG_PLL_DIV1 + 1) / mipitx_con1.RG_PLL_DIV2;
#endif
	return DSI_STATUS_OK;
}

DSI_STATUS DSI_Change_CLK(unsigned int clk)
{

	if (clk > 1000)
		return DSI_STATUS_ERROR;
	memcpy((void *)&lcm_params_for_clk_setting, (void *)lcm_params, sizeof(LCM_PARAMS));

	for (lcm_params_for_clk_setting.dsi.pll_div2 = 15;
	     lcm_params_for_clk_setting.dsi.pll_div2 > 0;
	     lcm_params_for_clk_setting.dsi.pll_div2--) {
		for (lcm_params_for_clk_setting.dsi.pll_div1 = 0;
		     lcm_params_for_clk_setting.dsi.pll_div1 < 39;
		     lcm_params_for_clk_setting.dsi.pll_div1++) {
			if ((13 * (lcm_params_for_clk_setting.dsi.pll_div1 + 1) /
			     lcm_params_for_clk_setting.dsi.pll_div2) >= clk)
				goto end;
		}
	}

	if (lcm_params_for_clk_setting.dsi.pll_div2 == 0) {
		for (lcm_params_for_clk_setting.dsi.pll_div1 = 0;
		     lcm_params_for_clk_setting.dsi.pll_div1 < 39;
		     lcm_params_for_clk_setting.dsi.pll_div1++) {
			if ((26 * (lcm_params_for_clk_setting.dsi.pll_div1 + 1)) >= clk)
				goto end;
		}
	}

end:
	_WaitForEngineNotBusy();
	DSI_PHY_clk_setting(&lcm_params_for_clk_setting);
	DSI_PHY_TIMCONFIG(&lcm_params_for_clk_setting);
	return DSI_STATUS_OK;
}


DSI_STATUS DSI_Capture_Framebuffer(unsigned int pvbuf, unsigned int bpp, bool cmd_mode)
{
	unsigned int mva;
#ifdef CONFIG_MTK_M4U
	unsigned int ret = 0;
	M4U_PORT_STRUCT portStruct;
#endif

	struct disp_path_config_mem_out_struct mem_out = { 0 };

	pr_info("enter DSI_Capture_FB!\n");

	if (bpp == 32)
		mem_out.outFormat = eARGB8888;
	else if (bpp == 16)
		mem_out.outFormat = eRGB565;
	else if (bpp == 24)
		mem_out.outFormat = eRGB888;
	else
		pr_info("DSI_Capture_FB, fb color format not support\n");

	pr_info("before alloc MVA: va = 0x%x, size = %d\n", pvbuf,
		lcm_params->height * lcm_params->width * bpp / 8);

#ifdef CONFIG_MTK_M4U
	ret =
	    m4u_alloc_mva(DISP_WDMA, pvbuf, lcm_params->height * lcm_params->width * bpp / 8, 0, 0,
			  &mva);
	if (ret != 0) {
		pr_info("m4u_alloc_mva() fail!\n");
		return DSI_STATUS_OK;
	}
	pr_info("addr=0x%x, format=%d\n", mva, (mem_out.outFormat));

	m4u_dma_cache_maint(DISP_WDMA,
			    (const void *)pvbuf,
			    lcm_params->height * lcm_params->width * bpp / 8, DMA_BIDIRECTIONAL);

	portStruct.ePortID = DISP_WDMA;	/* hardware port ID, defined in M4U_PORT_ID_ENUM */
	portStruct.Virtuality = 1;
	portStruct.Security = 0;
	portStruct.domain = 0;	/* domain : 0 1 2 3 */
	portStruct.Distance = 1;
	portStruct.Direction = 0;
	m4u_config_port(&portStruct);
#else
	mva = __pa(pvbuf);
#endif

	mem_out.enable = 1;
	mem_out.dstAddr = mva;
	mem_out.srcROI.x = 0;
	mem_out.srcROI.y = 0;
	mem_out.srcROI.height = lcm_params->height;
	mem_out.srcROI.width = lcm_params->width;

	_WaitForEngineNotBusy();
	disp_path_get_mutex();
	disp_path_config_mem_out(&mem_out);
	pr_info("Wait DSI idle\n");

	if (cmd_mode)
		DSI_Start();

	disp_path_release_mutex();

	_WaitForEngineNotBusy();
/* msleep(20); */
	disp_path_get_mutex();
	mem_out.enable = 0;
	disp_path_config_mem_out(&mem_out);

	if (cmd_mode)
		DSI_Start();

	disp_path_release_mutex();

#ifdef CONFIG_MTK_M4U
	portStruct.ePortID = DISP_WDMA;	/* hardware port ID, defined in M4U_PORT_ID_ENUM */
	portStruct.Virtuality = 1;
	portStruct.Security = 0;
	portStruct.domain = 0;	/* domain : 0 1 2 3 */
	portStruct.Distance = 1;
	portStruct.Direction = 0;
	m4u_config_port(&portStruct);

	m4u_dealloc_mva(DISP_WDMA, pvbuf, lcm_params->height * lcm_params->width * bpp / 8, mva);
#endif

	return DSI_STATUS_OK;
}


DSI_STATUS DSI_TE_Enable(bool enable)
{
	pr_info("sxk==>set TE Enable %d\n", enable);
	dsiTeEnable = enable;

	return DSI_STATUS_OK;
}

DSI_STATUS DSI_TE_EXT_Enable(bool enable)
{

	dsiTeExtEnable = enable;

	if (dsiTeExtEnable == false)
		OUTREGBIT(DSI_TXRX_CTRL_REG, DSI_REG->DSI_TXRX_CTRL, EXT_TE_EN, 0);

	return DSI_STATUS_OK;
}

bool DSI_Get_EXT_TE(void)
{
	return dsiTeExtEnable;
}

bool DSI_Get_BTA_TE(void)
{
	return dsiTeEnable;
}

DSI_STATUS DSI_Wait_VDO_Idle(void)
{
	static const long WAIT_TIMEOUT = 2 * HZ;	/* 2 sec */
	long ret;

	DSI_SetMode(0);

	ret = wait_event_interruptible_timeout(_dsi_wait_vm_done_queue,
					       !_IsEngineBusy(), WAIT_TIMEOUT);

	if (0 == ret) {
		DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
			       " Wait for DSI engine read ready timeout!!!\n");

		DSI_DumpRegisters();
		/* /do necessary reset here */
		DSI_Reset();
	}
	DSI_SetMode(lcm_params->dsi.mode);

	return DSI_STATUS_OK;
}

/* This part is for Display Customization Tool ********************/


DSI_MODE_CTRL_REG fb_config_mode_ctl, fb_config_mode_ctl_backup;

void fbconfig_set_cmd_mode(void)
{
	long ret;
	/*backup video mode */
	static const long FB_WAIT_TIMEOUT = 2 * HZ;

	OUTREG32(&fb_config_mode_ctl_backup, AS_UINT32(&DSI_REG->DSI_MODE_CTRL));
	OUTREG32(&fb_config_mode_ctl, AS_UINT32(&DSI_REG->DSI_MODE_CTRL));
	/*set to cmd mode */
	fb_config_mode_ctl.MODE = 0;
	OUTREG32(&DSI_REG->DSI_MODE_CTRL, AS_UINT32(&fb_config_mode_ctl));
	/* DSI_Reset(); */
	wait_vm_done_irq = true;
	ret = wait_event_interruptible_timeout(_dsi_wait_vm_done_queue,
					       !_IsEngineBusy(), FB_WAIT_TIMEOUT);
	if (0 == ret) {
		DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
			       " Wait for DSI engine read ready timeout!!!\n");

		DSI_DumpRegisters();
		/* do necessary reset here */
		DSI_Reset();
		wait_vm_done_irq = false;
		return;
	}
}

void fbconfig_set_vdo_mode(void)
{
	OUTREG32(&DSI_REG->DSI_MODE_CTRL, AS_UINT32(&fb_config_mode_ctl_backup));
}

void fbconfig_DSI_Continuous_HS(int enable)
{
	DSI_TXRX_CTRL_REG tmp_reg = DSI_REG->DSI_TXRX_CTRL;

	tmp_reg.HSTX_CKLP_EN = enable;
	OUTREG32(&DSI_REG->DSI_TXRX_CTRL, AS_UINT32(&tmp_reg));
}

unsigned int fbconfig_dsi_clk = 0;
DSI_STATUS fbconfig_DSI_set_CLK(unsigned int clk)
{
	LCM_PARAMS fb_lcm_params;

	pr_info("sxk==>fbconfig_DSI_set_CLK:%d\n", clk);
	if (lcm_params->dsi.PLL_CLOCK != 0) {
		if (clk > 1000)
			return DSI_STATUS_ERROR;
		memcpy((void *)&fb_lcm_params, (void *)lcm_params, sizeof(LCM_PARAMS));
		fb_lcm_params.dsi.PLL_CLOCK = clk;
		pr_info("dsi_set_clk==>fbconfig_DSI_set_CLK:will wait!!\n");

		_WaitForEngineNotBusy();
		pr_info("dsi_set_clk==>will fbconfig_DSI_set_CLK:%d\n", clk);

		DSI_PHY_clk_setting(&fb_lcm_params);
		DSI_PHY_TIMCONFIG(&fb_lcm_params);
		fbconfig_dsi_clk = clk;
	} else {

		memcpy((void *)&fb_lcm_params, (void *)lcm_params, sizeof(LCM_PARAMS));
		fb_lcm_params.dsi.pll_div1 = clk & 0x00000600;
		fb_lcm_params.dsi.pll_div2 = clk & 0x00000180;
		fb_lcm_params.dsi.fbk_div = clk & 0x0000007F;
		pr_info("dsi_set_clk==>fbconfig_DSI_set_CLK:will wait!!\n");
		_WaitForEngineNotBusy();
		pr_info("fbconfig_DSI_set_CLK:div1=>%d div2=>%d fbk_div=>%d\n",
			fb_lcm_params.dsi.pll_div1, fb_lcm_params.dsi.pll_div2,
			fb_lcm_params.dsi.fbk_div);
		DSI_PHY_clk_setting(&fb_lcm_params);
		DSI_PHY_TIMCONFIG(&fb_lcm_params);

	}
	OUTREGBIT(MIPITX_DSI_PLL_CHG_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CHG,
		  RG_DSI0_MPPLL_SDM_PCW_CHG, 0);
	OUTREGBIT(MIPITX_DSI_PLL_CHG_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CHG,
		  RG_DSI0_MPPLL_SDM_PCW_CHG, 1);

	DSI_DumpRegisters();
	return DSI_STATUS_OK;
}


unsigned int fbconfig_dsi_ssc = 0;

DSI_STATUS fbconfig_DSI_set_SSC(unsigned int ssc)
{
	LCM_PARAMS fb_lcm_params;

	pr_info("sxk==>fbconfig_DSI_set_SSC:%d\n", ssc);

	if ((ssc > 8) || (ssc < 1))
		return DSI_STATUS_ERROR;

	memcpy((void *)&fb_lcm_params, (void *)lcm_params, sizeof(LCM_PARAMS));
	fb_lcm_params.dsi.ssc_range = ssc;
	pr_info("sxk==>fbconfig_DSI_set_SSC:will wait!!\n");

	_WaitForEngineNotBusy();
	pr_info("sxk==>will fbconfig_DSI_set_SSC:%d\n", ssc);

	DSI_PHY_clk_setting(&fb_lcm_params);
	fbconfig_dsi_ssc = ssc;

	DSI_DumpRegisters();
	return DSI_STATUS_OK;
}

int fbconfig_dsi_lane_num = 0;

void fbconfig_DSI_set_lane_num(unsigned int lane_num)
{
	DSI_TXRX_CTRL_REG tmp_reg;

	tmp_reg = DSI_REG->DSI_TXRX_CTRL;
	switch (lane_num) {
	case LCM_ONE_LANE:
		tmp_reg.LANE_NUM = 1;
		break;
	case LCM_TWO_LANE:
		tmp_reg.LANE_NUM = 3;
		break;
	case LCM_THREE_LANE:
		tmp_reg.LANE_NUM = 0x7;
		break;
	case LCM_FOUR_LANE:
		tmp_reg.LANE_NUM = 0xF;
		break;
	}
	OUTREG32(&DSI_REG->DSI_TXRX_CTRL, AS_UINT32(&tmp_reg));

	if (lane_num > 0) {
		DSI_PHY_REG->MIPITX_DSI0_DATA_LANE0.RG_DSI0_LNT0_RT_CODE = 0x8;
		DSI_PHY_REG->MIPITX_DSI0_DATA_LANE0.RG_DSI0_LNT0_LDOOUT_EN = 1;
	}

	if (lane_num > 1) {
		DSI_PHY_REG->MIPITX_DSI0_DATA_LANE1.RG_DSI0_LNT1_RT_CODE = 0x8;
		DSI_PHY_REG->MIPITX_DSI0_DATA_LANE1.RG_DSI0_LNT1_LDOOUT_EN = 1;
	}

	if (lane_num > 2) {
		DSI_PHY_REG->MIPITX_DSI0_DATA_LANE2.RG_DSI0_LNT2_RT_CODE = 0x8;
		DSI_PHY_REG->MIPITX_DSI0_DATA_LANE2.RG_DSI0_LNT2_LDOOUT_EN = 1;
	}

	if (lane_num > 3) {
		DSI_PHY_REG->MIPITX_DSI0_DATA_LANE3.RG_DSI0_LNT3_RT_CODE = 0x8;
		DSI_PHY_REG->MIPITX_DSI0_DATA_LANE3.RG_DSI0_LNT3_LDOOUT_EN = 1;
	}
	fbconfig_dsi_lane_num = lane_num;

}

static unsigned int g_New_HPW;

void fbconfig_DSI_set_timing(MIPI_TIMING timing)
{

	int fbconfig_dsiTmpBufBpp = 0;

	if (lcm_params->dsi.data_format.format == LCM_DSI_FORMAT_RGB565)
		fbconfig_dsiTmpBufBpp = 2;
	else
		fbconfig_dsiTmpBufBpp = 3;


	switch (timing.type) {
	case SSC_EN:
		OUTREGBIT(MIPITX_DSI_PLL_CON1_REG, DSI_PHY_REG->MIPITX_DSI_PLL_CON1,
			  RG_DSI0_MPPLL_SDM_SSC_EN, timing.value);
		break;
	case LPX:
		OUTREGBIT(DSI_PHY_TIMCON0_REG, DSI_REG->DSI_PHY_TIMECON0, LPX, timing.value);
		break;
	case HS_PRPR:
		OUTREGBIT(DSI_PHY_TIMCON0_REG, DSI_REG->DSI_PHY_TIMECON0, HS_PRPR, timing.value);
		break;
	case HS_ZERO:
		OUTREGBIT(DSI_PHY_TIMCON0_REG, DSI_REG->DSI_PHY_TIMECON0, HS_ZERO, timing.value);
		break;
	case HS_TRAIL:
		OUTREGBIT(DSI_PHY_TIMCON0_REG, DSI_REG->DSI_PHY_TIMECON0, HS_TRAIL, timing.value);
		break;
	case TA_GO:
		OUTREGBIT(DSI_PHY_TIMCON1_REG, DSI_REG->DSI_PHY_TIMECON1, TA_GO, timing.value);
		break;
	case TA_SURE:
		OUTREGBIT(DSI_PHY_TIMCON1_REG, DSI_REG->DSI_PHY_TIMECON1, TA_SURE, timing.value);
		break;
	case TA_GET:
		OUTREGBIT(DSI_PHY_TIMCON1_REG, DSI_REG->DSI_PHY_TIMECON1, TA_GET, timing.value);
		break;
	case DA_HS_EXIT:
		OUTREGBIT(DSI_PHY_TIMCON1_REG, DSI_REG->DSI_PHY_TIMECON1, DA_HS_EXIT, timing.value);
		break;
	case CONT_DET:
		OUTREGBIT(DSI_PHY_TIMCON2_REG, DSI_REG->DSI_PHY_TIMECON2, CONT_DET, timing.value);
		break;
	case CLK_ZERO:
		OUTREGBIT(DSI_PHY_TIMCON2_REG, DSI_REG->DSI_PHY_TIMECON2, CLK_ZERO, timing.value);
		break;
	case CLK_TRAIL:
		OUTREGBIT(DSI_PHY_TIMCON2_REG, DSI_REG->DSI_PHY_TIMECON2, CLK_TRAIL, timing.value);
		break;
	case CLK_HS_PRPR:
		OUTREGBIT(DSI_PHY_TIMCON3_REG, DSI_REG->DSI_PHY_TIMECON3, CLK_HS_PRPR,
			  timing.value);
		break;
	case CLK_HS_POST:
		OUTREGBIT(DSI_PHY_TIMCON3_REG, DSI_REG->DSI_PHY_TIMECON3, CLK_HS_POST,
			  timing.value);
		break;
	case CLK_HS_EXIT:
		OUTREGBIT(DSI_PHY_TIMCON3_REG, DSI_REG->DSI_PHY_TIMECON3, CLK_HS_EXIT,
			  timing.value);
		break;
	case HPW:
		if (lcm_params->dsi.mode == SYNC_EVENT_VDO_MODE
		    || lcm_params->dsi.mode == BURST_VDO_MODE) {
			/* do nothing */
			g_New_HPW = timing.value;
		} else {
			g_New_HPW = timing.value;
			timing.value = (timing.value * fbconfig_dsiTmpBufBpp - 10);
			OUTREG32(&DSI_REG->DSI_HSA_WC, ALIGN_TO((timing.value), 4));
		}

		break;
	case HFP:
		timing.value = (timing.value * fbconfig_dsiTmpBufBpp - 12);
		OUTREG32(&DSI_REG->DSI_HFP_WC, ALIGN_TO((timing.value), 4));
		break;
	case HBP:
		if (g_New_HPW == 0)
			g_New_HPW = lcm_params->dsi.horizontal_sync_active;

		if (lcm_params->dsi.mode == SYNC_EVENT_VDO_MODE
		    || lcm_params->dsi.mode == BURST_VDO_MODE) {
			timing.value = ((timing.value + g_New_HPW) * fbconfig_dsiTmpBufBpp - 10);
		} else {
			timing.value = (timing.value * fbconfig_dsiTmpBufBpp - 10);
		}
		OUTREG32(&DSI_REG->DSI_HBP_WC, ALIGN_TO((timing.value), 4));
		break;
	case VPW:
		OUTREG32(&DSI_REG->DSI_VACT_NL, timing.value);
		break;
	case VFP:
		OUTREG32(&DSI_REG->DSI_VFP_NL, timing.value);
		break;
	case VBP:
		OUTREG32(&DSI_REG->DSI_VBP_NL, timing.value);
		break;
	default:
		pr_info("fbconfig dsi set timing :no such type!!\n");
	}
	DSI_DumpRegisters();
}


int fbconfig_get_Continuous_status(void)
{
	int ret;
	DSI_TXRX_CTRL_REG tmp_reg;

	OUTREG32(&tmp_reg, AS_UINT32(&DSI_REG->DSI_TXRX_CTRL));
	ret = tmp_reg.HSTX_CKLP_EN;
	return ret;
}

int fbconfig_get_dsi_CLK(void)
{
	unsigned char div1 = 0;
	unsigned char div2 = 0;
	unsigned int fbk_div = 0;

	if (lcm_params->dsi.PLL_CLOCK != 0) {
		if (fbconfig_dsi_clk == 0)
			return lcm_params->dsi.PLL_CLOCK;
		else
			return fbconfig_dsi_clk;
	} else {
		div1 = DSI_PHY_REG->MIPITX_DSI_PLL_CON0.RG_DSI0_MPPLL_TXDIV0;
		div2 = DSI_PHY_REG->MIPITX_DSI_PLL_CON0.RG_DSI0_MPPLL_TXDIV1;
		fbk_div = (DSI_PHY_REG->MIPITX_DSI_PLL_CON2.RG_DSI0_MPPLL_SDM_PCW_H) >> 2;

		return (div1 << 9) | (div2 << 7) | fbk_div;
	}

}

int fbconfig_get_dsi_ssc(void)
{
	if (fbconfig_dsi_ssc == 0)
		return ((lcm_params->dsi.ssc_range) ? lcm_params->dsi.ssc_range : 5);	/* default is 5; */
	else
		return fbconfig_dsi_ssc;
}


int fbconfig_get_dsi_lane_num(void)
{
	if (fbconfig_dsi_lane_num == 0)
		return lcm_params->dsi.LANE_NUM;
	else
		return fbconfig_dsi_lane_num;
}


int fbconfig_get_dsi_timing(MIPI_SETTING_TYPE type)
{
	int fbconfig_dsiTmpBufBpp = 0;

	if (lcm_params->dsi.data_format.format == LCM_DSI_FORMAT_RGB565)
		fbconfig_dsiTmpBufBpp = 2;
	else
		fbconfig_dsiTmpBufBpp = 3;

	switch (type) {
	case SSC_EN:
		return DSI_PHY_REG->MIPITX_DSI_PLL_CON1.RG_DSI0_MPPLL_SDM_SSC_EN;
	case LPX:
		return DSI_REG->DSI_PHY_TIMECON0.LPX;
	case HS_PRPR:
		return DSI_REG->DSI_PHY_TIMECON0.HS_PRPR;
	case HS_ZERO:
		return DSI_REG->DSI_PHY_TIMECON0.HS_ZERO;
	case HS_TRAIL:
		return DSI_REG->DSI_PHY_TIMECON0.HS_TRAIL;
	case TA_GO:
		return DSI_REG->DSI_PHY_TIMECON1.TA_GO;
		/* OUTREGBIT(DSI_PHY_TIMCON1_REG,DSI_REG->DSI_PHY_TIMECON1,TA_GO,timing.value); */

	case TA_SURE:
		return DSI_REG->DSI_PHY_TIMECON1.TA_SURE;
		/* OUTREGBIT(DSI_PHY_TIMCON1_REG,DSI_REG->DSI_PHY_TIMECON1,TA_SURE,timing.value); */

	case TA_GET:
		return DSI_REG->DSI_PHY_TIMECON1.TA_GET;
	case DA_HS_EXIT:
		return DSI_REG->DSI_PHY_TIMECON1.DA_HS_EXIT;
		/* OUTREGBIT(DSI_PHY_TIMCON1_REG,DSI_REG->DSI_PHY_TIMECON1,DA_HS_EXIT,timing.value); */

	case CONT_DET:
		return DSI_REG->DSI_PHY_TIMECON2.CONT_DET;
		/* OUTREGBIT(DSI_PHY_TIMCON2_REG,DSI_REG->DSI_PHY_TIMECON2,CONT_DET,timing.value); */
		/* break; */
	case CLK_ZERO:
		return DSI_REG->DSI_PHY_TIMECON2.CLK_ZERO;
		/* OUTREGBIT(DSI_PHY_TIMCON2_REG,DSI_REG->DSI_PHY_TIMECON2,CLK_ZERO,timing.value); */
		/* break; */
	case CLK_TRAIL:
		return DSI_REG->DSI_PHY_TIMECON2.CLK_TRAIL;
		/* OUTREGBIT(DSI_PHY_TIMCON2_REG,DSI_REG->DSI_PHY_TIMECON2,CLK_TRAIL,timing.value); */
		/* break; */
	case CLK_HS_PRPR:
		return DSI_REG->DSI_PHY_TIMECON3.CLK_HS_PRPR;
		/* OUTREGBIT(DSI_PHY_TIMCON3_REG,DSI_REG->DSI_PHY_TIMECON3,CLK_HS_PRPR,timing.value); */
		/* break; */
	case CLK_HS_POST:
		return DSI_REG->DSI_PHY_TIMECON3.CLK_HS_POST;
		/* OUTREGBIT(DSI_PHY_TIMCON3_REG,DSI_REG->DSI_PHY_TIMECON3,CLK_HS_POST,timing.value); */
		/* break; */
	case CLK_HS_EXIT:
		return DSI_REG->DSI_PHY_TIMECON3.CLK_HS_EXIT;
		/* OUTREGBIT(DSI_PHY_TIMCON3_REG,DSI_REG->DSI_PHY_TIMECON3,CLK_HS_EXIT,timing.value); */
		/* break; */
	case HPW:
		{
			DSI_HSA_WC_REG tmp_reg;

			OUTREG32(&tmp_reg, AS_UINT32(&DSI_REG->DSI_HSA_WC));
			if (lcm_params->dsi.mode == SYNC_EVENT_VDO_MODE
			    || lcm_params->dsi.mode == BURST_VDO_MODE)
				return 0;	/* in such mode there is no HSA register ; */
			else
				return ((tmp_reg.HSA_WC + 10) / fbconfig_dsiTmpBufBpp);
		}

		/* OUTREG32(&DSI_REG->DSI_HSA_WC, ALIGN_TO((timing.value), 4)); */
		/* break; */
	case HFP:
		{
			DSI_HFP_WC_REG tmp_hfp;

			OUTREG32(&tmp_hfp, AS_UINT32(&DSI_REG->DSI_HFP_WC));
			return ((tmp_hfp.HFP_WC + 12) / fbconfig_dsiTmpBufBpp);
		}
		/* OUTREG32(&DSI_REG->DSI_HFP_WC, ALIGN_TO((timing.value), 4)); */
		/* break; */
	case HBP:
		{
			DSI_HBP_WC_REG tmp_hbp;

			if (g_New_HPW == 0)
				g_New_HPW = lcm_params->dsi.horizontal_sync_active;
			OUTREG32(&tmp_hbp, AS_UINT32(&DSI_REG->DSI_HBP_WC));
			if (lcm_params->dsi.mode == SYNC_EVENT_VDO_MODE
			    || lcm_params->dsi.mode == BURST_VDO_MODE)
				return ((tmp_hbp.HBP_WC + 10) / fbconfig_dsiTmpBufBpp - g_New_HPW);
			else
				return ((tmp_hbp.HBP_WC + 10) / fbconfig_dsiTmpBufBpp);
		}
		/* OUTREG32(&DSI_REG->DSI_HBP_WC, ALIGN_TO((timing.value), 4)); */
		/* break; */
	case VPW:
		{
			DSI_VACT_NL_REG tmp_vpw;

			OUTREG32(&tmp_vpw, AS_UINT32(&DSI_REG->DSI_VACT_NL));
			return tmp_vpw.VACT_NL;
		}
		/* OUTREG32(&DSI_REG->DSI_VACT_NL,timing.value); */
		/* break; */
	case VFP:
		{
			DSI_VFP_NL_REG tmp_vfp;

			OUTREG32(&tmp_vfp, AS_UINT32(&DSI_REG->DSI_VFP_NL));
			return tmp_vfp.VFP_NL;
		}
		/* OUTREG32(&DSI_REG->DSI_VFP_NL, timing.value); */
		/* break; */
	case VBP:
		{
			DSI_VBP_NL_REG tmp_vbp;

			OUTREG32(&tmp_vbp, AS_UINT32(&DSI_REG->DSI_VBP_NL));
			return tmp_vbp.VBP_NL;
		}
		/* OUTREG32(&DSI_REG->DSI_VBP_NL, timing.value); */
		/* break; */
	default:
		pr_info("fbconfig dsi set timing :no such type!!\n");
	}
	return 0;
}

int fbconfig_get_TE_enable(void)
{
	return dsiTeEnable;
}


uint32_t fbconfig_dsi_dcs_read_lcm_reg_v2(uint8_t cmd, uint8_t type, uint8_t *buffer,
					  uint8_t buffer_size)
{
	uint32_t max_try_count = 5;
	uint32_t recv_data_cnt;
	unsigned int read_timeout_ms;
	unsigned char packet_type;
	DSI_RX_DATA_REG read_data0;
	DSI_RX_DATA_REG read_data1;
	DSI_RX_DATA_REG read_data2;
	DSI_RX_DATA_REG read_data3;
#if 1
	DSI_T0_INS t0;

#if ENABLE_DSI_INTERRUPT
	static const long WAIT_TIMEOUT = 2 * HZ;	/* 2 sec */
	long ret;
#endif
	if (DSI_REG->DSI_MODE_CTRL.MODE)
		return 0;

	if (buffer == NULL || buffer_size == 0)
		return 0;

	do {
		if (max_try_count == 0)
			return 0;
		max_try_count--;
		recv_data_cnt = 0;
		read_timeout_ms = 20;

		_WaitForEngineNotBusy();

		t0.CONFG = 0x04;	/* /BTA */
		t0.Data0 = cmd;
		if (type == 0)
			t0.Data_ID = DSI_DCS_READ_PACKET_ID;
		else if (type == 1)
			t0.Data_ID = DSI_GERNERIC_READ_LONG_PACKET_ID;
		t0.Data1 = 0;

		OUTREG32(&DSI_CMDQ_REG->data[0], AS_UINT32(&t0));
		OUTREG32(&DSI_REG->DSI_CMDQ_SIZE, 1);

		/* /clear read ACK */
		/* DSI_REG->DSI_RACK.DSI_RACK = 1; */
		/* DSI_REG->DSI_INTSTA.RD_RDY = 1; */
		/* DSI_REG->DSI_INTSTA.CMD_DONE = 1; */
		/* DSI_REG->DSI_INTEN.RD_RDY =  1; */
		/* DSI_REG->DSI_INTEN.CMD_DONE=  1; */
		OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);
		OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, RD_RDY, 1);
		OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, CMD_DONE, 1);
		OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, RD_RDY, 1);
		OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, CMD_DONE, 1);



		OUTREG32(&DSI_REG->DSI_START, 0);
		OUTREG32(&DSI_REG->DSI_START, 1);

		/* / the following code is to */
		/* / 1: wait read ready */
		/* / 2: ack read ready */
		/* / 3: wait for CMDQ_DONE */
		/* / 3: read data */
#if ENABLE_DSI_INTERRUPT
		ret = wait_event_interruptible_timeout(_dsi_dcs_read_wait_queue,
						       !_IsEngineBusy(), WAIT_TIMEOUT);
		if (0 == ret) {
			DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
				       " Wait for DSI engine read ready timeout!!!\n");

			DSI_DumpRegisters();

			/* /do necessary reset here */
			/* DSI_REG->DSI_RACK.DSI_RACK = 1; */
			OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);
			DSI_Reset();

			return 0;
		}
#else
#ifdef DDI_DRV_DEBUG_LOG_ENABLE
		if (dsi_log_on)
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI",
				       " Start polling DSI read ready!!!\n");
#endif
		while (DSI_REG->DSI_INTSTA.RD_RDY == 0)	{
			/* /keep polling */
			msleep(20);
			read_timeout_ms--;

			if (read_timeout_ms == 0) {
				DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI",
					       " Polling DSI read ready timeout!!!\n");
				DSI_DumpRegisters();

				/* /do necessary reset here */
				/* DSI_REG->DSI_RACK.DSI_RACK = 1; */
				OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);
				DSI_Reset();
				return 0;
			}
		}
#ifdef DDI_DRV_DEBUG_LOG_ENABLE
		if (dsi_log_on)
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " End polling DSI read ready!!!\n");
#endif

		/* DSI_REG->DSI_RACK.DSI_RACK = 1; */
		OUTREGBIT(DSI_RACK_REG, DSI_REG->DSI_RACK, DSI_RACK, 1);

		/* /clear interrupt status */
		/* DSI_REG->DSI_INTSTA.RD_RDY = 1; */
		OUTREGBIT(DSI_INT_STATUS_REG, DSI_REG->DSI_INTSTA, RD_RDY, 1);
		/* /STOP DSI */
		OUTREG32(&DSI_REG->DSI_START, 0);

#endif

		/* DSI_REG->DSI_INTEN.RD_RDY =  0; */
		OUTREGBIT(DSI_INT_ENABLE_REG, DSI_REG->DSI_INTEN, RD_RDY, 1);

		OUTREG32(&read_data0, AS_UINT32(&DSI_REG->DSI_RX_DATA0));
		OUTREG32(&read_data1, AS_UINT32(&DSI_REG->DSI_RX_DATA1));
		OUTREG32(&read_data2, AS_UINT32(&DSI_REG->DSI_RX_DATA2));
		OUTREG32(&read_data3, AS_UINT32(&DSI_REG->DSI_RX_DATA3));
#ifdef DDI_DRV_DEBUG_LOG_ENABLE
		if (dsi_log_on) {
			unsigned int i;
/* DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_STA : 0x%x\n", DSI_REG->DSI_RX_STA); */
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_CMDQ_SIZE : 0x%x\n",
				       DSI_REG->DSI_CMDQ_SIZE.CMDQ_SIZE);
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_CMDQ_DATA0 : 0x%x\n",
				       DSI_CMDQ_REG->data[0].byte0);
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_CMDQ_DATA1 : 0x%x\n",
				       DSI_CMDQ_REG->data[0].byte1);
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_CMDQ_DATA2 : 0x%x\n",
				       DSI_CMDQ_REG->data[0].byte2);
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_CMDQ_DATA3 : 0x%x\n",
				       DSI_CMDQ_REG->data[0].byte3);
#if 1
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_DATA0 : 0x%x\n",
				       DSI_REG->DSI_RX_DATA0);
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_DATA1 : 0x%x\n",
				       DSI_REG->DSI_RX_DATA1);
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_DATA2 : 0x%x\n",
				       DSI_REG->DSI_RX_DATA2);
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI_RX_DATA3 : 0x%x\n",
				       DSI_REG->DSI_RX_DATA3);

			pr_info("read_data0, %x,%x,%x,%x\n", read_data0.byte0, read_data0.byte1,
				read_data0.byte2, read_data0.byte3);
			pr_info("read_data1, %x,%x,%x,%x\n", read_data1.byte0, read_data1.byte1,
				read_data1.byte2, read_data1.byte3);
			pr_info("read_data2, %x,%x,%x,%x\n", read_data2.byte0, read_data2.byte1,
				read_data2.byte2, read_data2.byte3);
			pr_info("read_data3, %x,%x,%x,%x\n", read_data3.byte0, read_data3.byte1,
				read_data3.byte2, read_data3.byte3);
#endif
		}
#endif

#if 1
		packet_type = read_data0.byte0;

#ifdef DDI_DRV_DEBUG_LOG_ENABLE
		if (dsi_log_on)
			DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI", " DSI read packet_type is 0x%x\n",
				       packet_type);
#endif



		if (packet_type == 0x1A || packet_type == 0x1C) {
			recv_data_cnt = read_data0.byte1 + read_data0.byte2 * 16;
			if (recv_data_cnt > 10) {
#ifdef DDI_DRV_DEBUG_LOG_ENABLE
				if (dsi_log_on)
					DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
						       " DSI read long packet data  exceeds 4 bytes\n");
#endif
				recv_data_cnt = 10;
			}

			if (recv_data_cnt > buffer_size) {
#ifdef DDI_DRV_DEBUG_LOG_ENABLE
				if (dsi_log_on)
					DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
						       " DSI read long packet data  exceeds buffer size: %d\n",
						       buffer_size);
#endif
				recv_data_cnt = buffer_size;
			}
#ifdef DDI_DRV_DEBUG_LOG_ENABLE
			if (dsi_log_on)
				DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
					       " DSI read long packet size: %d\n", recv_data_cnt);
#endif
			memcpy((void *)buffer, (void *)&read_data1, buffer_size);
		} else {
			if (recv_data_cnt > buffer_size) {
#ifdef DDI_DRV_DEBUG_LOG_ENABLE
				if (dsi_log_on)
					DISP_LOG_PRINT(ANDROID_LOG_WARN, "DSI",
						       " DSI read short packet data  exceeds buffer size: %d\n",
						       buffer_size);
#endif
				recv_data_cnt = buffer_size;
			}
			memcpy((void *)buffer, (void *)&read_data0.byte1, 2);
		}
#endif
	} while (packet_type != 0x1C && packet_type != 0x21 && packet_type != 0x22
		 && packet_type != 0x1A);
	/* / here: we may receive a ACK packet which packet type is 0x02 (incdicates some error happened) */
	/* / therefore we try re-read again until no ACK packet */
	/* / But: if it is a good way to keep re-trying ??? */
#endif
	return recv_data_cnt;
}
