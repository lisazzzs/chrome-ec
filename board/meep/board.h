/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Meep/Mimrock board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Select Baseboard features */
#define VARIANT_OCTOPUS_EC_NPCX796FB
#define VARIANT_OCTOPUS_CHARGER_ISL9238
#include "baseboard.h"

#define CONFIG_VOLUME_BUTTONS
#define GPIO_VOLUME_UP_L GPIO_EC_VOLUP_BTN_ODL
#define GPIO_VOLUME_DOWN_L GPIO_EC_VOLDN_BTN_ODL

/* EC console commands  */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO

#define CONFIG_LED_COMMON
#define OCTOPUS_POWER_LED

/* Sensors */
#define CONFIG_ACCEL_KX022		/* Lid accel */
#define CONFIG_ACCELGYRO_LSM6DSM	/* Base accel */
#define CONFIG_SENSORHUB_LSM6DSM
#define CONFIG_MAG_LIS2MDL
/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK (1 << LID_ACCEL)
#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT

/* USB PD */
#undef CONFIG_USB_PD_VBUS_MEASURE_NOT_PRESENT
#define CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL
#define CONFIG_LID_ANGLE_TABLET_MODE
#define CONFIG_LID_ANGLE_INVALID_CHECK

#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_13K7_47K_4050B
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B

#define CONFIG_ACCEL_INTERRUPTS
/* FIFO size is in power of 2. */
#define CONFIG_ACCEL_FIFO 1024

/* Depends on how fast the AP boots and typical ODRs */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO / 3)
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_HOST_EVENT

#define OCTOPUS_BATT_FUEL_LOW_LED 10

#define CONFIG_ACCEL_LSM6DSM_INT_EVENT TASK_EVENT_CUSTOM(4)
#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_TEMP_SENSOR_AMB,		/* ADC0 */
	ADC_TEMP_SENSOR_CHARGER,	/* ADC1 */
	ADC_VBUS_C1,			/* ADC4 */
	ADC_VBUS_C0,			/* ADC9 */
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_BATTERY,
	TEMP_SENSOR_AMBIENT,
	TEMP_SENSOR_CHARGER,
	TEMP_SENSOR_COUNT
};

enum pwm_channel {
	PWM_CH_KBLIGHT,
	PWM_CH_COUNT
};

/* Motion sensors */
enum sensor_id {
	LID_ACCEL,
	BASE_ACCEL,
	BASE_GYRO,
	BASE_MAG,
	SENSOR_COUNT
};

enum battery_type {
	BATTERY_DANAPACK_COS,
	BATTERY_DANAPACK_ATL,
	BATTERY_DANAPACK_SDI,
	BATTERY_SAMSUNG_SDI,
	BATTERY_SIMPLO_COS,
	BATTERY_SIMPLO_ATL,
	BATTERY_TYPE_COUNT,
};

int board_is_convertible(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
