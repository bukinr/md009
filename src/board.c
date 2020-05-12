/*-
 * Copyright (c) 2018-2020 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/console.h>
#include <sys/callout.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/thread.h>

#include <arm/arm/nvic.h>
#include <arm/nordicsemi/nrf9160.h>

#include <dev/gpio/gpio.h>
#include <dev/intc/intc.h>
#include <dev/uart/uart.h>

#include "board.h"
#include "sensor.h"

static struct arm_nvic_softc nvic_sc;
static struct nrf_uarte_softc uarte_sc;
static struct nrf_power_softc power_sc;
static struct nrf_timer_softc timer0_sc;
static struct nrf_twim_softc twim1_sc;
static struct nrf_gpio_softc gpio0_sc;
struct nrf_gpiote_softc gpiote1_sc;

struct mdx_device dev_i2c  = { .arg = &twim1_sc };
struct mdx_device dev_nvic = { .arg =  &nvic_sc };
struct mdx_device dev_uart = { .arg = &uarte_sc };
struct mdx_device dev_gpio = { .arg = &gpio0_sc };

void
board_init(void)
{
	struct nrf_twim_conf conf;
	struct nrf_gpiote_conf gconf;

	nrf_uarte_init(&dev_uart, BASE_UARTE0, UART_PIN_TX, UART_PIN_RX);
	mdx_uart_setup(&dev_uart, UART_BAUDRATE, UART_DATABITS_8,
	    UART_STOPBITS_1, UART_PARITY_NONE);
	mdx_console_register_uart(&dev_uart);

	mdx_fl_init();
	mdx_fl_add_region(0x20004000, 0x0c000);
	mdx_fl_add_region(0x20030000, 0x10000);

	nrf_power_init(&power_sc, BASE_POWER);
	nrf_timer_init(&timer0_sc, BASE_TIMER0, 1000000);
	nrf_gpio_init(&dev_gpio, BASE_GPIO);
	nrf_gpiote_init(&gpiote1_sc, BASE_GPIOTE1);

	arm_nvic_init(&dev_nvic, BASE_SCS);

	mdx_intc_setup(&dev_nvic, ID_UARTE0, nrf_uarte_intr, &uarte_sc);
	mdx_intc_setup(&dev_nvic, ID_TIMER0, nrf_timer_intr, &timer0_sc);
	mdx_intc_setup(&dev_nvic, ID_TWIM1, nrf_twim_intr, &twim1_sc);
	mdx_intc_setup(&dev_nvic, ID_GPIOTE1, nrf_gpiote_intr, &gpiote1_sc);

	mdx_intc_enable(&dev_nvic, ID_TIMER0);
	mdx_intc_enable(&dev_nvic, ID_UARTE0);
	mdx_intc_enable(&dev_nvic, ID_TWIM1);
	mdx_intc_enable(&dev_nvic, ID_GPIOTE1);

	conf.freq = TWIM_FREQ_K100;
	conf.pin_scl = PIN_MC_SCL;
	conf.pin_sda = PIN_MC_SDA;

	nrf_twim_init(&dev_i2c, BASE_TWIM1);
	nrf_twim_setup(&twim1_sc, &conf);

	nrf_gpio_pincfg(&dev_gpio, PIN_MC_INTA, 0);
	mdx_gpio_configure(&dev_gpio, 0, PIN_MC_INTA, MDX_GPIO_INPUT);

	/* Configure GPIOTE for mc6470. */
	gconf.pol = GPIOTE_POLARITY_HITOLO;
	gconf.mode = GPIOTE_MODE_EVENT;
	gconf.pin = PIN_MC_INTA;
	nrf_gpiote_config(&gpiote1_sc, MC6470_GPIOTE_CFG_ID, &gconf);

	printf("mdepx initialized\n");
}
