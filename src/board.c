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
#include <sys/of.h>

#include <arm/nordicsemi/nrf9160.h>

#include <dev/gpio/gpio.h>
#include <dev/intc/intc.h>
#include <dev/uart/uart.h>

#include "board.h"
#include "sensor.h"

void
board_init(void)
{
	struct nrf_gpiote_conf gconf;
	mdx_device_t dev;

	/* Add some memory so OF could allocate devices and their softc. */
	mdx_fl_init();
	mdx_fl_add_region((void *)0x20004000, 0x0c000);
	mdx_fl_add_region((void *)0x20030000, 0x10000);

	mdx_of_install_dtbp((void *)0xf8000);
	mdx_of_probe_devices();

	dev = mdx_device_lookup_by_name("nrf_gpio", 0);
	if (!dev)
		panic("gpio dev not found");
	nrf_gpio_pincfg(dev, PIN_MC_INTA, 0);
	mdx_gpio_configure(dev, PIN_MC_INTA, MDX_GPIO_INPUT);

	/* Configure GPIOTE for mc6470. */
	dev = mdx_device_lookup_by_name("nrf_gpiote", 0);
	if (!dev)
		panic("gpiote dev not found");
	gconf.pol = GPIOTE_POLARITY_HITOLO;
	gconf.mode = GPIOTE_MODE_EVENT;
	gconf.pin = PIN_MC_INTA;
	nrf_gpiote_config(dev, MC6470_GPIOTE_CFG_ID, &gconf);

	/* Enable the instruction cache. */
	dev = mdx_device_lookup_by_name("nrf_nvmc", 0);
	if (!dev)
		panic("nvmc dev not found");
	nrf_nvmc_icache_control(dev, true);

	printf("mdepx initialized\n");
}
