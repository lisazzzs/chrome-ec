/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Cheza board-specific configuration */

#include "adc_chip.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "extpower.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/ppc/sn5s330.h"
#include "driver/tcpm/anx74xx.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "pi3usb9281.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "system.h"
#include "shi_chip.h"
#include "switch.h"
#include "task.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define USB_PD_PORT_ANX3429	0
#define USB_PD_PORT_PS8751	1

/* Forward declaration */
static void tcpc_alert_event(enum gpio_signal signal);
static void vbus0_evt(enum gpio_signal signal);
static void vbus1_evt(enum gpio_signal signal);
static void usb0_evt(enum gpio_signal signal);
static void usb1_evt(enum gpio_signal signal);
static void ppc_interrupt(enum gpio_signal signal);
static void anx74xx_cable_det_interrupt(enum gpio_signal signal);

#include "gpio_list.h"

/* GPIO Interrupt Handlers */
static void tcpc_alert_event(enum gpio_signal signal)
{
	int port = -1;

	switch (signal) {
	case GPIO_USB_C0_PD_INT_ODL:
		port = 0;
		break;
	case GPIO_USB_C1_PD_INT_ODL:
		port = 1;
		break;
	default:
		return;
	}

	schedule_deferred_pd_interrupt(port);
}

static void vbus0_evt(enum gpio_signal signal)
{
	/* VBUS present GPIO is inverted */
	usb_charger_vbus_change(0, !gpio_get_level(GPIO_USB_C0_VBUS_DET_L));
	task_wake(TASK_ID_PD_C0);
}

static void vbus1_evt(enum gpio_signal signal)
{
	/* VBUS present GPIO is inverted */
	usb_charger_vbus_change(1, !gpio_get_level(GPIO_USB_C1_VBUS_DET_L));
	task_wake(TASK_ID_PD_C1);
}

static void usb0_evt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12, 0);
}

static void usb1_evt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P1, USB_CHG_EVENT_BC12, 0);
}

static void anx74xx_cable_det_handler(void)
{
	int cable_det = gpio_get_level(GPIO_USB_C0_CABLE_DET);
	int reset_n = gpio_get_level(GPIO_USB_C0_PD_RST_R_L);

	/*
	 * A cable_det low->high transition was detected. If following the
	 * debounce time, cable_det is high, and reset_n is low, then ANX3429 is
	 * currently in standby mode and needs to be woken up. Set the
	 * TCPC_RESET event which will bring the ANX3429 out of standby
	 * mode. Setting this event is gated on reset_n being low because the
	 * ANX3429 will always set cable_det when transitioning to normal mode
	 * and if in normal mode, then there is no need to trigger a tcpc reset.
	 */
	if (cable_det && !reset_n)
		task_set_event(TASK_ID_PD_C0, PD_EVENT_TCPC_RESET, 0);
}
DECLARE_DEFERRED(anx74xx_cable_det_handler);

static void anx74xx_cable_det_interrupt(enum gpio_signal signal)
{
	/* debounce for 2 msec */
	hook_call_deferred(&anx74xx_cable_det_handler_data, (2 * MSEC));
}

static void ppc_interrupt(enum gpio_signal signal)
{
	/* Only port-0 uses PPC chip */
	sn5s330_interrupt(0);
}

/* Wake-up pins for hibernate */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
	GPIO_EC_RST_ODL,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Base detection */
	[ADC_BASE_DET] = {
		"BASE_DET",
		NPCX_ADC_CH0,
		ADC_MAX_VOLT,
		ADC_READ_MAX + 1,
		0
	},
	/* Measure VBUS through a 1/10 voltage divider */
	[ADC_VBUS] = {
		"VBUS",
		NPCX_ADC_CH1,
		ADC_MAX_VOLT * 10,
		ADC_READ_MAX + 1,
		0
	},
	/*
	 * Adapter current output or battery charging/discharging current (uV)
	 * 18x amplification on charger side.
	 */
	[ADC_AMON_BMON] = {
		"AMON_BMON",
		NPCX_ADC_CH2,
		ADC_MAX_VOLT * 1000 / 18,
		ADC_READ_MAX + 1,
		0
	},
	/*
	 * ISL9238 PSYS output is 1.44 uA/W over 12.4K resistor, to read
	 * 0.8V @ 45 W, i.e. 56250 uW/mV. Using ADC_MAX_VOLT*56250 and
	 * ADC_READ_MAX+1 as multiplier/divider leads to overflows, so we
	 * only divide by 2 (enough to avoid precision issues).
	 */
	[ADC_PSYS] = {
		"PSYS",
		NPCX_ADC_CH3,
		ADC_MAX_VOLT * 56250 * 2 / (ADC_READ_MAX + 1),
		2,
		0
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct pwm_t pwm_channels[] = {
	/* TODO(waihong): Assign a proper frequency. */
	[PWM_CH_DISPLIGHT] = { 5, 0, 4800 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);


/* Power signal list. Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	[SDM845_AP_RST_L] = {
		GPIO_AP_RST_L,
		POWER_SIGNAL_ACTIVE_HIGH | POWER_SIGNAL_DISABLE_AT_BOOT,
		"AP_RST_L"},
	[SDM845_PS_HOLD] = {
		GPIO_PS_HOLD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"PS_HOLD"},
	[SDM845_PMIC_FAULT_L] = {
		GPIO_PMIC_FAULT_L,
		POWER_SIGNAL_ACTIVE_HIGH | POWER_SIGNAL_DISABLE_AT_BOOT,
		"PMIC_FAULT_L"},
	[SDM845_POWER_GOOD] = {
		GPIO_POWER_GOOD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"POWER_GOOD"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* I2C port map */
const struct i2c_port_t i2c_ports[] = {
	{"power",   I2C_PORT_POWER,  100, GPIO_I2C0_SCL,  GPIO_I2C0_SDA},
	/* TODO(b/78189419): ANX7428 operates at 400kHz initially. */
	{"tcpc0",   I2C_PORT_TCPC0,  400, GPIO_I2C1_SCL,  GPIO_I2C1_SDA},
	{"tcpc1",   I2C_PORT_TCPC1, 1000, GPIO_I2C2_SCL,  GPIO_I2C2_SDA},
	{"eeprom",  I2C_PORT_EEPROM, 400, GPIO_I2C5_SCL,  GPIO_I2C5_SDA},
	{"sensor",  I2C_PORT_SENSOR, 400, GPIO_I2C7_SCL,  GPIO_I2C7_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* Power Path Controller */
struct ppc_config_t ppc_chips[] = {
	{
		.i2c_port = I2C_PORT_TCPC0,
		.i2c_addr = SN5S330_ADDR0,
		.drv = &sn5s330_drv
	},
	/*
	 * Port 1 uses two power switches instead:
	 *   NX5P3290: to source VBUS
	 *   NX20P5090: to sink VBUS (charge battery)
	 * which are controlled directly by EC GPIOs.
	 */
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* TCPC mux configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_ANX3429] = {I2C_PORT_TCPC0, 0x50, &anx74xx_tcpm_drv,
				 TCPC_ALERT_ACTIVE_LOW, TCPC_ALERT_OPEN_DRAIN},
	[USB_PD_PORT_PS8751] = {I2C_PORT_TCPC1, 0x16, &ps8xxx_tcpm_drv,
				TCPC_ALERT_ACTIVE_LOW},
};

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.driver = &anx74xx_tcpm_usb_mux_driver,
		.hpd_update = &anx74xx_tcpc_update_hpd_status,
	},
	{
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	}
};

/* BC1.2 */
struct pi3usb9281_config pi3usb9281_chips[] = {
	{
		.i2c_port = I2C_PORT_POWER,
	},
	{
		.i2c_port = I2C_PORT_EEPROM,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9281_chips) ==
	     CONFIG_BC12_DETECT_PI3USB9281_CHIP_COUNT);

/* Initialize board. */
static void board_init(void)
{
	/* Enable BC1.2 VBUS detection */
	gpio_enable_interrupt(GPIO_USB_C0_VBUS_DET_L);
	gpio_enable_interrupt(GPIO_USB_C1_VBUS_DET_L);

	/* Enable BC1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_L);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_L);

	/* Enable interrupt for BMI160 sensor */
	gpio_enable_interrupt(GPIO_ACCEL_GYRO_INT_L);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void board_tcpc_init(void)
{
	int port;

	/* Only reset TCPC if not sysjump */
	if (!system_jumped_to_this_image()) {
		/* TODO(crosbug.com/p/61098): How long do we need to wait? */
		board_reset_pd_mcu();
	}

	/* Enable PPC interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_SWCTL_INT_ODL);

	/* Enable TCPC interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);

	/* Enable CABLE_DET interrupt for ANX3429 wake from standby */
	gpio_enable_interrupt(GPIO_USB_C0_CABLE_DET);

	/*
	 * Initialize HPD to low; after sysjump SOC needs to see
	 * HPD pulse to enable video path
	 */
	for (port = 0; port < CONFIG_USB_PD_PORT_COUNT; port++) {
		const struct usb_mux *mux = &usb_muxes[port];

		mux->hpd_update(port, 0, 0);
	}
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C+1);

static void board_chipset_suspend(void)
{
	/*
	 * Turn off display backlight in S3. AP has its own control. The EC's
	 * and the AP's will be AND'ed together in hardware.
	 */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

static void board_chipset_resume(void)
{
	/* Turn on display backlight in S0. */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/**
 * Power on (or off) a single TCPC.
 * minimum on/off delays are included.
 *
 * @param port  Port number of TCPC.
 * @param mode  0: power off, 1: power on.
 */
void board_set_tcpc_power_mode(int port, int mode)
{
	if (port != USB_PD_PORT_ANX3429)
		return;

	if (mode) {
		gpio_set_level(GPIO_EN_USB_C0_TCPC_PWR, 1);
		msleep(ANX74XX_PWR_H_RST_H_DELAY_MS);
		gpio_set_level(GPIO_USB_C0_PD_RST_R_L, 1);
	} else {
		gpio_set_level(GPIO_USB_C0_PD_RST_R_L, 0);
		msleep(ANX74XX_RST_L_PWR_L_DELAY_MS);
		gpio_set_level(GPIO_EN_USB_C0_TCPC_PWR, 0);
		msleep(ANX74XX_PWR_L_PWR_H_DELAY_MS);
	}
}

void board_reset_pd_mcu(void)
{
	/* Assert reset */
	gpio_set_level(GPIO_USB_C0_PD_RST_R_L, 0);
	gpio_set_level(GPIO_USB_C1_PD_RST_ODL, 0);

	msleep(MAX(1, ANX74XX_RST_L_PWR_L_DELAY_MS));
	gpio_set_level(GPIO_USB_C1_PD_RST_ODL, 1);
	/* Disable TCPC0 (anx3429) power */
	gpio_set_level(GPIO_EN_USB_C0_TCPC_PWR, 0);

	msleep(ANX74XX_PWR_L_PWR_H_DELAY_MS);
	board_set_tcpc_power_mode(USB_PD_PORT_ANX3429, 1);
}

int board_vbus_sink_enable(int port, int enable)
{
	if (port == USB_PD_PORT_ANX3429) {
		/* Port 0 is controlled by a PPC SN5S330 */
		return ppc_vbus_sink_enable(port, enable);
	} else if (port == USB_PD_PORT_PS8751) {
		/* Port 1 is controlled by a power switch NX20P5090 */
		gpio_set_level(GPIO_EN_USB_C1_CHARGE_EC_L, !enable);
		return EC_SUCCESS;
	}
	return EC_ERROR_INVAL;
}

int board_is_sourcing_vbus(int port)
{
	if (port == USB_PD_PORT_ANX3429) {
		/* Port 0 is controlled by a PPC SN5S330 */
		return ppc_is_sourcing_vbus(port);
	} else if (port == USB_PD_PORT_PS8751) {
		/* Port 1 is controlled by a power switch NX5P3290 */
		return gpio_get_level(GPIO_EN_USB_C1_5V_OUT);
	}
	return EC_ERROR_INVAL;
}

void board_overcurrent_event(int port)
{
	/* TODO(waihong): Notify AP? */
	CPRINTS("p%d: overcurrent!", port);
}

int board_set_active_charge_port(int port)
{
	int is_real_port = (port >= 0 &&
			    port < CONFIG_USB_PD_PORT_COUNT);
	int i;
	int rv;

	if (!is_real_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	CPRINTS("New chg p%d", port);

	if (port == CHARGE_PORT_NONE) {
		/* Disable all ports. */
		for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; i++) {
			rv = board_vbus_sink_enable(i, 0);
			if (rv) {
				CPRINTS("Disabling p%d sink path failed.", i);
				return rv;
			}
		}

		return EC_SUCCESS;
	}

	/* Check if the port is sourcing VBUS. */
	if (board_is_sourcing_vbus(port)) {
		CPRINTF("Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; i++) {
		if (i == port)
			continue;

		if (board_vbus_sink_enable(i, 0))
			CPRINTS("p%d: sink path disable failed.", i);
	}

	/* Enable requested charge port. */
	if (board_vbus_sink_enable(port, 1)) {
		CPRINTS("p%d: sink path enable failed.");
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	/*
	 * Ignore lower charge ceiling on PD transition if our battery is
	 * critical, as we may brownout.
	 */
	if (supplier == CHARGE_SUPPLIER_PD &&
	    charge_ma < 1500 &&
	    charge_get_percent() < CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON) {
		CPRINTS("Using max ilim %d", max_ma);
		charge_ma = max_ma;
	}

	charge_set_input_current_limit(MAX(charge_ma,
					   CONFIG_CHARGER_INPUT_CURRENT),
				       charge_mv);
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL))
		if (gpio_get_level(GPIO_USB_C0_PD_RST_R_L))
			status |= PD_STATUS_TCPC_ALERT_0;
	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL))
		if (gpio_get_level(GPIO_USB_C1_PD_RST_ODL))
			status |= PD_STATUS_TCPC_ALERT_1;

	return status;
}

/* Mutexes */
static struct mutex g_lid_mutex;

static struct bmi160_drv_data_t g_bmi160_data;

/* Matrix to rotate accelerometer into standard reference frame */
const mat33_fp_t base_standard_ref = {
	{ FLOAT_TO_FP(-1), 0,  0},
	{ 0,  FLOAT_TO_FP(-1),  0},
	{ 0,  0, FLOAT_TO_FP(1)}
};

struct motion_sensor_t motion_sensors[] = {
	/*
	 * Note: bmi160: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	[LID_ACCEL] = {
	 .name = "Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3_S5,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bmi160_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_SENSOR,
	 .addr = BMI160_ADDR0,
	 .rot_standard_ref = &base_standard_ref,
	 .default_range = 4,  /* g */
	 .min_frequency = BMI160_ACCEL_MIN_FREQ,
	 .max_frequency = BMI160_ACCEL_MAX_FREQ,
	 .config = {
		 [SENSOR_CONFIG_EC_S0] = {
			 .odr = 10000 | ROUND_UP_FLAG,
		 },
	 },
	},
	[LID_GYRO] = {
	 .name = "Gyro",
	 .active_mask = SENSOR_ACTIVE_S0_S3_S5,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bmi160_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_SENSOR,
	 .addr = BMI160_ADDR0,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = &base_standard_ref,
	 .min_frequency = BMI160_GYRO_MIN_FREQ,
	 .max_frequency = BMI160_GYRO_MAX_FREQ,
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);
