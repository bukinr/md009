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

#include <sys/mbuf.h>
#include <net/if.h>
#include <net/netinet/in.h>
#include <arpa/inet.h>

#include <arm/nordicsemi/nrf9160.h>

#include <nrfxlib/bsdlib/include/bsd_platform.h>
#include <nrfxlib/bsdlib/include/nrf_socket.h>
#include <nrfxlib/bsdlib/include/bsd.h>
#include <nrfxlib/bsdlib/include/bsd_os.h>

#include <dev/gpio/gpio.h>

#include "app.h"
#include "board.h"
#include "sensor.h"
#include "gps.h"
#include "lte.h"
#include "mqtt.h"
#include "tls.h"

#define	GNSS_EPHEMERIDES	(1 << 0)
#define	GNSS_ALMANAC		(1 << 1)
#define	GNSS_IONOSPHERIC	(1 << 2)
#define	GNSS_LGF		(1 << 3) /* Last good fix */
#define	GNSS_GPS_TOW		(1 << 4) /* Time of week */
#define	GNSS_GPS_WN		(1 << 5) /* Week number */
#define	GNSS_LEAP_SECOND	(1 << 6)
#define	GNSS_LOCAL_CLOCK_FOD	(1 << 7) /* frequency offset data */

#define	LC_MAX_READ_LENGTH	128
#define	AT_CMD_SIZE(x)		(sizeof(x) - 1)

static const char cind[] __unused = "AT+CIND?";
static const char subscribe[] = "AT+CEREG=5";
static const char lock_bands[] __unused =
    "AT\%XBANDLOCK=2,\"10000001000000001100\"";
static const char normal[] = "AT+CFUN=1";
static const char flight[] __unused = "AT+CFUN=4";
static const char gps_enable[] __unused = "AT+CFUN=31";
static const char lte_enable[] __unused = "AT+CFUN=21";
static const char lte_disable[] __unused = "AT+CFUN=20";
static const char edrx_req[] __unused = "AT+CEDRXS=1,4,\"1000\"";
static const char cgact[] __unused = "AT+CGACT=1,1";
static const char cgatt[] __unused = "AT+CGATT=1";
static const char cgdcont[] __unused = "AT+CGDCONT?";
static const char cgdcont_req[] __unused =
    "AT+CGDCONT=1,\"IP\",\"ibasis.iot\"";
static const char cgpaddr[] __unused = "AT+CGPADDR";
static const char cesq[] __unused = "AT+CESQ";
static const char cpsms[] __unused = "AT+CPSMS=";

static const char psm_req[] = "AT+CPSMS=1,,,\"00000110\",\"00000000\"";
/* Request eDRX to be disabled */
static const char edrx_disable[] = "AT+CEDRXS=3";

int get_random_number(uint8_t *out, int size);

/*
 * %XSYSTEMMODE=<M1_support>,<NB1_support>,<GNSS_support>,<LTE_preference>
 */

static const char systm_mode[] __unused = "AT%XSYSTEMMODE?";
static const char nbiot_gps[] __unused = "AT%XSYSTEMMODE=0,1,1,0";
static const char catm1_gps[] __unused = "AT%XSYSTEMMODE=1,0,1,0";

static char buffer[LC_MAX_READ_LENGTH];
static int buffer_fill;
static int ready_to_send;
static mdx_device_t gpio;

static void
sw_ctl(bool gps_enable, bool onboard_antenna)
{
	uint32_t reg;

	reg = CNF_DIR_OUT | CNF_INPUT_DIS | CNF_PULL_DOWN;

	/*
	 * SW1: GPS antenna switch
	 * 0: u.FL
	 * 1: MN
	 */
	nrf_gpio_pincfg(gpio, PIN_SW1_CTL, reg);
	mdx_gpio_configure(gpio, PIN_SW1_CTL, MDX_GPIO_OUTPUT);

	/*
	 * SW2: LTE antenna switch
	 * 0: MN
	 * 1: u.FL
	 */
	nrf_gpio_pincfg(gpio, PIN_SW2_CTL, reg);
	mdx_gpio_configure(gpio, PIN_SW2_CTL, MDX_GPIO_OUTPUT);

	/*
	 * SW2: Fractus antenna switch
	 * 0: LTE
	 * 1: GPS
	 */
	nrf_gpio_pincfg(gpio, PIN_SW3_CTL, reg);
	mdx_gpio_configure(gpio, PIN_SW3_CTL, MDX_GPIO_OUTPUT);

	/* GPS Amplifier */
	nrf_gpio_pincfg(gpio, PIN_GPS_AMP_EN, reg);
	mdx_gpio_configure(gpio, PIN_GPS_AMP_EN, MDX_GPIO_OUTPUT);

	/* LED1 */
	nrf_gpio_pincfg(gpio, PIN_LED1, reg);
	mdx_gpio_configure(gpio, PIN_LED1, MDX_GPIO_OUTPUT);
	mdx_gpio_set(gpio, PIN_LED1, 1);

	/* LED2 */
	nrf_gpio_pincfg(gpio, PIN_LED2, reg);
	mdx_gpio_configure(gpio, PIN_LED2, MDX_GPIO_OUTPUT);
	mdx_gpio_set(gpio, PIN_LED2, 1);

	if (gps_enable == false) {
		/* LTE antenna */
		if (onboard_antenna)
			mdx_gpio_set(gpio, PIN_SW2_CTL, 0);
		else
			mdx_gpio_set(gpio, PIN_SW2_CTL, 1);
		mdx_gpio_set(gpio, PIN_SW3_CTL, 0);
		mdx_gpio_set(gpio, PIN_GPS_AMP_EN, 0);
	} else {
		/* GPS antenna */
		mdx_gpio_set(gpio, PIN_SW3_CTL, 1);
		if (onboard_antenna)
			mdx_gpio_set(gpio, PIN_SW1_CTL, 1);
		else
			mdx_gpio_set(gpio, PIN_SW1_CTL, 0);
		mdx_gpio_set(gpio, PIN_GPS_AMP_EN, 1);
	}
}

static int
at_send(int fd, const char *cmd, size_t size)
{
	int len;

	len = nrf_send(fd, cmd, size, 0);
	if (len != size) {
		printf("send failed\n");
		return (-1);
	}

	return (0);
}

static int
at_recv(int fd, char *buf, int bufsize)
{
	int len;

	len = nrf_recv(fd, buf, bufsize, 0);

	return (len);
}

static int
at_cmd(int fd, const char *cmd, size_t size)
{
	char buffer[LC_MAX_READ_LENGTH];
	int len;

	printf("send: %s\n", cmd);

	if (at_send(fd, cmd, size) == 0) {
		len = at_recv(fd, buffer, LC_MAX_READ_LENGTH);
		if (len)
			printf("recv: %s\n", buffer);
	}

	return (0);
}

static void
lte_signal(int fd)
{
	char buf[LC_MAX_READ_LENGTH];
	float rsrq;
	int rsrp;
	int len;
	char *t, *p;

	/* Extended signal quality */
	at_send(fd, cesq, AT_CMD_SIZE(cesq));
	len = at_recv(fd, buf, LC_MAX_READ_LENGTH);
	if (len) {
		printf("recv: %s\n", buf);

		t = (char *)buf;

		p = strsep(&t, ",");	/* +CESQ: rxlev */
		p = strsep(&t, ",");	/* ber */
		p = strsep(&t, ",");	/* rscp */
		p = strsep(&t, ",");	/* echo */
		p = strsep(&t, ",");	/* rsrq */

		rsrq = 20 - atoi(p) / 2;

		p = strsep(&t, ",");

		rsrp = 140 - atoi(p) + 1;

		printf("LTE signal quality: rsrq -%.2f dB rsrp -%d dBm\n",
		    rsrq, rsrp);
	}
}

static void __unused
lte_at_client(void *arg)
{
	int fd;
	int len;

	fd = nrf_socket(NRF_AF_LTE, NRF_SOCK_DGRAM, NRF_PROTO_AT);
	if (fd < 0) {
		printf("failed to create socket\n");
		return;
	}

	while (1) {
		if (ready_to_send) {
			nrf_send(fd, buffer, buffer_fill, 0);

			ready_to_send = 0;
			buffer_fill = 0;

			len = nrf_recv(fd, buffer, LC_MAX_READ_LENGTH, 0);
			if (len)
				printf("%s\n", buffer);
		}
		mdx_usleep(10000);
	}
}

static int __unused
check_ipaddr(char *buf)
{
	char *t;
	char *p;

	t = (char *)buf;

	printf("%s: %s\n", __func__, buf);

	p = strsep(&t, ",");
	if (p == NULL || strcmp(p, "+CGDCONT: 0") != 0)
		return (0);

	p = strsep(&t, ",");
	if (p == NULL || strcmp(p, "\"IP\"") != 0)
		return (0);

	p = strsep(&t, ",");
	if (p == NULL || strcmp(p, "\"\"") == 0)
		return (0);

	printf("APN: %s\n", p);

	p = strsep(&t, ",");
	if (p == NULL || strcmp(p, "\"\"") == 0)
		return (0);

	printf("IP: %s\n", p);

	/* Success */

	return (1);
}

static int
lte_wait(int fd)
{
	char buf[LC_MAX_READ_LENGTH];
	int len;
	char *t;
	char *p;

	printf("Awaiting registration in the LTE-M network...\n");

	while (1) {
		len = at_recv(fd, buf, LC_MAX_READ_LENGTH);
		if (len) {
			printf("recv: %s\n", buf);
			t = (char *)buf;
			p = strsep(&t, ",");
			if (p != NULL) {
				/* Check network registration status. */

				if (strcmp(p, "+CEREG: 3") == 0) {
					printf("Registration denied\n");
					return (-1);
				}

				if (strcmp(p, "+CEREG: 5") == 0) {
					printf("Registered, roaming.\n");
					break;
				}

				if (strcmp(p, "+CEREG: 1") == 0) {
					printf("Registered, home network.\n");
					break;
				}
			}
		}

		mdx_usleep(1000000);
	}

	lte_signal(fd);

	return (0);
}

int
lte_connect(void)
{
	int err;
	int fd;

	printf("%s\n", __func__);

	fd = nrf_socket(NRF_AF_LTE, NRF_SOCK_DGRAM, NRF_PROTO_AT);
	if (fd < 0) {
		printf("failed to create socket\n");
		return (-1);
	}

	printf("AT lte socket %d\n", fd);

	/* Switch to the flight mode. */
	at_cmd(fd, flight, AT_CMD_SIZE(flight));

	/* Read current system mode. */
	at_cmd(fd, systm_mode, AT_CMD_SIZE(systm_mode));

	/* Set new system mode */
	at_cmd(fd, catm1_gps, AT_CMD_SIZE(catm1_gps));

	/* Switch to power saving mode as required for GPS to operate. */
	at_cmd(fd, psm_req, AT_CMD_SIZE(psm_req));

	at_cmd(fd, cind, AT_CMD_SIZE(cind));
	at_cmd(fd, edrx_req, AT_CMD_SIZE(edrx_req));

	/* Lock bands 3,4,13,20. */
	at_cmd(fd, lock_bands, AT_CMD_SIZE(lock_bands));

	/* Subscribe for events. */
	at_send(fd, subscribe, AT_CMD_SIZE(subscribe));

	/* Switch to normal mode. */
	at_cmd(fd, normal, AT_CMD_SIZE(normal));

	if (lte_wait(fd) == 0) {
		printf("LTE connected\n");
		err = 0;
	} else {
		printf("Failed to connect to LTE\n");
		err = -1;
	}

	nrf_close(fd);

	return (err);
}

static int
gps_en(void)
{
	int fd;

	fd = nrf_socket(NRF_AF_LTE, NRF_SOCK_DGRAM, NRF_PROTO_AT);
	if (fd < 0) {
		printf("failed to create socket\n");
		return (-1);
	}

	/* Switch to GPS */
	sw_ctl(true, true);

	mdx_usleep(500000);

	/* Switch to GPS mode. */
	at_cmd(fd, lte_disable, AT_CMD_SIZE(lte_disable));

	mdx_usleep(500000);

	at_cmd(fd, edrx_disable, AT_CMD_SIZE(edrx_disable));

	mdx_usleep(500000);

	/* Switch to GPS mode. */
	at_cmd(fd, gps_enable, AT_CMD_SIZE(gps_enable));

	mdx_usleep(500000);

	return (0);
}

static void
nrf_input(int c, void *arg)
{

	if (c == 13)
		ready_to_send = 1;
	else if (buffer_fill < LC_MAX_READ_LENGTH)
		buffer[buffer_fill++] = c;
}

int
main(void)
{
	bsd_init_params_t init_params;
	mdx_device_t uart;
	int error;

	uart = mdx_device_lookup_by_name("nrf_uarte", 0);
	if (!uart)
		panic("uart dev not found");
	nrf_uarte_register_callback(uart, nrf_input, NULL);

	gpio = mdx_device_lookup_by_name("nrf_gpio", 0);
	if (!gpio)
		panic("gpio dev not found");

#if 0
	uint8_t rand[4];
	int err;
	while (1) {
		printf("Getting random number...\n");
		err = get_random_number(rand, 4);
		printf("err %d, result %x\n", err, rand[0]);
	}
#endif

	/* Switch to LTE */
	sw_ctl(false, true);

	init_params.trace_on = true;
	init_params.bsd_memory_address = BSD_RESERVED_MEMORY_ADDRESS;
	init_params.bsd_memory_size = BSD_RESERVED_MEMORY_SIZE;
	bsd_init(&init_params);

	printf("bsd library initialized\n");

	buffer_fill = 0;
	ready_to_send = 0;

	sensor_init();
	mdx_usleep(100000);

	app1();

	mqtt_test();

	gps_en();
	error = gps_init();
	if (error)
		printf("Can't initialize GPS\n");
	else {
		printf("GPS initialized\n");
		gps_test();
	}

	while (1)
		mdx_usleep(1000000);

	return (0);
}
