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
#include <mbedtls/platform_util.h>
#include <mbedtls/entropy.h>

void
mbedtls_platform_zeroize(void *buf, size_t len)
{

	memset(buf, 0, len);
}

#if 0
int
mbedtls_entropy_func(void *data, unsigned char *output, size_t len)
{

	/* TODO */

	printf("%s: data %p len %d\n", __func__, data, len);

	memcpy(output, (void *)(0x20020000 + len), len);

	return (0);
}
#endif

int mbedtls_platform_entropy_poll( void *data,
    unsigned char *output, size_t len, size_t *olen );

int
mbedtls_platform_entropy_poll( void *data,
    unsigned char *output, size_t len, size_t *olen )
{

	*olen = 0;

	if(len < sizeof(unsigned char))
		return( 0 );

	*olen = sizeof(unsigned char);

	return (0);
}
