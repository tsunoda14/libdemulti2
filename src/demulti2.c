/*
 *  MULTI2 Descrambling Library for BCAS
 *
 *  Copyright (C) 2013 tsunoda14
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.=
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "demulti2.h"

#if (CONFIG_SOBACAS || CONFIG_PCSC)
#include "bcas_cardio.h"
#endif

#if HAVE_ENDIAN_H
#include <endian.h>
#elif HAVE_SYS_ENDIAN_H
#include <sys/endian.h>
#endif

#if HAVE_BETOH64
#define be64toh(x) betoh64((x))
#endif


typedef uint8_t   u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef struct u32x2 {
#if WORDS_BIGENDIAN
	u32 l;
	u32 r;
#else
	u32 r;
	u32 l;
#endif
} /* __attribute__ ((packed, aligned(4))) */ CORE_DATA;


typedef union demulti2_key {
	u64 whole;
	CORE_DATA sub;
} m2key_t;

struct demulti2_key_info {
	m2key_t k_scr[2];
	u32 wrk[2][8]; /*wrk[0][]:odd,	wrk[1][]:even */
	enum {
		KEY_S_EMPTY,
		KEY_S_OLD,
		KEY_S_OK
	} status;
};

#define MAX_PID 0x2000

struct demulti2_context {
	struct demulti2_key_info *ecm_kinfo[MAX_PID];
	void *lib;
	enum {
		MODE_YAKISOBA,
		MODE_PCSC,
	} mode;

	struct demulti2_param {
		u32 round;
		u32 k_sys[8];
		union demulti2_key cbc_init;
	} init_param;

#if (CONFIG_SOBACAS || CONFIG_PCSC)
	bcas_card_t *card;
#endif

	int have_avx2;
};

extern void descramble2(u8 * obuf, const u8 * ibuf, u32 * prm, u32 len);

/*
 * libyakisoba functions (for dlopen)
 */
int (*bcas_decodeECM)(const uint8_t *Payload, uint32_t Size, uint8_t *Keys, uint8_t *VarPart);


/* internal utility functions */

static inline u32
left_rotate_uint32 (u32 val, u32 count)
{
	return ((val << count) | (val >> (32 - count)));
}

static void
core_pi1 (CORE_DATA * dst, CORE_DATA * src)
{
	dst->l = src->l;
	dst->r = src->r ^ src->l;
}

static void
core_pi2 (CORE_DATA * dst, CORE_DATA * src, u32 a)
{
	u32 t0, t1, t2;

	t0 = src->r + a;
	t1 = left_rotate_uint32 (t0, 1) + t0 - 1;
	t2 = left_rotate_uint32 (t1, 4) ^ t1;

	dst->l = src->l ^ t2;
	dst->r = src->r;
}

static void
core_pi3 (CORE_DATA * dst, CORE_DATA * src, u32 a, u32 b)
{
	u32 t0, t1, t2, t3, t4, t5;

	t0 = src->l + a;
	t1 = left_rotate_uint32 (t0, 2) + t0 + 1;
	t2 = left_rotate_uint32 (t1, 8) ^ t1;
	t3 = t2 + b;
	t4 = left_rotate_uint32 (t3, 1) - t3;
	t5 = left_rotate_uint32 (t4, 16) ^ (t4 | src->l);

	dst->l = src->l;
	dst->r = src->r ^ t5;
}

static void
core_pi4 (CORE_DATA * dst, CORE_DATA * src, u32 a)
{
	u32 t0, t1;

	t0 = src->r + a;
	t1 = left_rotate_uint32 (t0, 2) + t0 + 1;

	dst->l = src->l ^ t1;
	dst->r = src->r;
}


void
core_schedule (u32 * work, u32 * skey, CORE_DATA * dkey)
{
	CORE_DATA b1, b2, b3, b4, b5, b6, b7, b8, b9;

	core_pi1 (&b1, dkey);

	core_pi2 (&b2, &b1, skey[0]);
	work[0] = b2.l;

	core_pi3 (&b3, &b2, skey[1], skey[2]);
	work[1] = b3.r;

	core_pi4 (&b4, &b3, skey[3]);
	work[2] = b4.l;

	core_pi1 (&b5, &b4);
	work[3] = b5.r;

	core_pi2 (&b6, &b5, skey[4]);
	work[4] = b6.l;

	core_pi3 (&b7, &b6, skey[5], skey[6]);
	work[5] = b7.r;

	core_pi4 (&b8, &b7, skey[7]);
	work[6] = b8.l;

	core_pi1 (&b9, &b8);
	work[7] = b9.r;
}


static void
core_encrypt (CORE_DATA * dst, CORE_DATA * src, u32 * w, int round)
{
	int i;

	CORE_DATA tmp;

	dst->l = src->l;
	dst->r = src->r;
	for (i = 0; i < round; i++) {
		core_pi1 (&tmp, dst);
		core_pi2 (dst, &tmp, w[0]);
		core_pi3 (&tmp, dst, w[1], w[2]);
		core_pi4 (dst, &tmp, w[3]);
		core_pi1 (&tmp, dst);
		core_pi2 (dst, &tmp, w[4]);
		core_pi3 (&tmp, dst, w[5], w[6]);
		core_pi4 (dst, &tmp, w[7]);
	}
}


static void
core_decrypt (CORE_DATA * dst, CORE_DATA * src, u32 * w, int round)
{
	int i;

	CORE_DATA tmp;

	dst->l = src->l;
	dst->r = src->r;
	for (i = 0; i < round; i++) {
		core_pi4 (&tmp, dst, w[7]);
		core_pi3 (dst, &tmp, w[5], w[6]);
		core_pi2 (&tmp, dst, w[4]);
		core_pi1 (dst, &tmp);
		core_pi4 (&tmp, dst, w[3]);
		core_pi3 (dst, &tmp, w[1], w[2]);
		core_pi2 (&tmp, dst, w[0]);
		core_pi1 (dst, &tmp);
	}
}

static void
descramble (const u8 * ibuf, int len, u8 * obuf, u32 * prm, int round, u64 init)
{
	m2key_t src, dst, cbc;

	cbc.whole = init;

	while (len >= 8) {
		src.whole = be64toh (*(u64 *) ibuf);
		core_decrypt (&dst.sub, &src.sub, prm, round);
		dst.whole ^= cbc.whole;
		cbc.whole = src.whole;
		*(u64 *) obuf = htobe64 (dst.whole);
		len -= 8;
		ibuf += 8;
		obuf += 8;
	}

	if (len > 0) {
		int i;
		u64 t64;
		u8 *tmp = (u8 *) & t64;

		core_encrypt (&dst.sub, &cbc.sub, prm, round);
		t64 = htobe64 (dst.whole);

		for (i = 0; i < len; i++)
			obuf[i] = ibuf[i] ^ tmp[i];
	}

	return;
}


Demulti2Handle
demulti2_open (void)
{
	const char *env;
	Demulti2Handle h;

	h = (Demulti2Handle) calloc (1, sizeof (struct demulti2_context));
	if (!h)
		return NULL;

	h->init_param.round = 4,
	h->init_param.k_sys[0] = 0x36310466; // store in native endian, not BE
	h->init_param.k_sys[1] = 0x4b17ea5c;
	h->init_param.k_sys[2] = 0x32df9cf5;
	h->init_param.k_sys[3] = 0xc4c36c1b;
	h->init_param.k_sys[4] = 0xec993921;
	h->init_param.k_sys[5] = 0x689d4bb7;
	h->init_param.k_sys[6] = 0xb74e4084;
	h->init_param.k_sys[7] = 0x0d2e7d98;
	h->init_param.cbc_init.whole = 0xfe27199919690911ULL;

#ifdef __x86_64__
	/* runtime check AVX2 */
	__asm__ ("pushq %%rbx\n\t"
		"movl $1, %%eax\n\t"
		"cpuid\n\t"
		"andl $0x18000000, %%ecx\n\t"
		"cmpl $0x18000000, %%ecx\n\t"
		"jne not_supported\n\t"
		"movl $7, %%eax\n\t"
		"movl $0, %%ecx\n\t"
		"cpuid\n\t"
		"andl $0x20, %%ebx\n\t"
		"cmpl $0x20, %%ebx\n\t"
		"jne not_supported\n\t"
		"movl $0, %%ecx\n\t"
		"xgetbv\n\t"
		"andl $0x06, %%eax\n\t"
		"cmpl $0x06, %%eax\n\t"
		"jne not_supported\n\t"
		"movl $1, %0\n\t"
		"jmp done\n"
		"not_supported:\n\t"
		"movl $0, %0\n"
		"done:\n\t"
		"popq %%rbx"
		: "=r" (h->have_avx2) : : "%rax", "%rbx", "%rcx", "%rdx");
#endif

	env = getenv ("DEMULTI2_MODE");
#if CONFIG_YAKISOBA
	if (!env || !strncmp (env, "yakisoba", strlen ("yakisoba"))) {
		h->mode = MODE_YAKISOBA;
		h->lib = dlopen ("libyakisoba.so.0", RTLD_LAZY);
		if (h->lib) {
			bcas_decodeECM = dlsym (h->lib, "bcas_decodeECM");
			if (bcas_decodeECM)
				return h;
			dlclose (h->lib);
			h->lib = NULL;
		}
	}
#endif

#if CONFIG_SOBACAS
	if (!env || !strncmp (env, "sobacas", strlen ("sobacas"))) {
		h->mode = MODE_PCSC; /* libsobacas should have the same API with PC/SC. */
		h->lib = dlopen ("libsobacas.so.0", RTLD_LAZY);
		if (h->lib) {
			h->card = bcas_card_init (h->lib);
			/* FIXME: copy back init parameters out of h->card */
			if (h->card)
				return h;
			dlclose (h->lib);
			h->lib = NULL;
		}
	}
#endif

#if CONFIG_PCSC
	if (!env || !strncmp (env, "pcsc", strlen ("pcsc"))) {
		h->mode = MODE_PCSC;
		h->lib = dlopen ("libpcsclite.so.1", RTLD_LAZY);
		if (h->lib) {
			h->card = bcas_card_init (h->lib);
			/* FIXME: copy back init parameters out of h->card */
			if (h->card)
				return h;
			dlclose (h->lib);
			h->lib = NULL;
		}
	}
#endif

	free (h);
	return NULL;
}

void
demulti2_close (Demulti2Handle h)
{
	int i;

	if (!h)
		return;

#if (CONFIG_SOBACAS || CONFIG_PCSC)
	if (h->card)
		bcas_card_stop (h->card);
#endif

	for (i = 0; i < MAX_PID; i++)
		if (h->ecm_kinfo[i])
			free (h->ecm_kinfo[i]);

	if (h->lib)
		dlclose (h->lib);

	free(h);
}

int
demulti2_descramble (Demulti2Handle h, const uint8_t * src, int len,
		uint8_t sc, uint16_t ecm_pid, uint8_t * dst)
{
	uint8_t ca_flags;

	if (!h)
		return DEMULTI2_E_INV_HANDLE;
	ca_flags = sc >> 6;
	if (src == NULL || ecm_pid >= 0x1FFF || len > 184 || ca_flags < 0x02)
		return DEMULTI2_E_INV_ARG;

	/*
	 * Note:
	 *  Because an ECM usually replaces just one of Keven and Kodd,
	 * one of them stays valid even after an failur of an ECM decoding.
	 * The valid one is usually the key for descrambling the current packets
	 * (for the moment), as an ECM should update the key for the future,
	 * not for the current.
	 *  Thus we accept keys marked KEY_S_OLD as well as KEY_S_OK here.
	 * Naturally the old valid key gets invalid after the next fed ECM
	 * failed again, but in those cases, both keys are makred KEY_S_EMPTY
	 * in demulti2_feed_ecm();
	 */
	if (!h->ecm_kinfo[ecm_pid]
	    || h->ecm_kinfo[ecm_pid]->status == KEY_S_EMPTY)
		return DEMULTI2_E_NOT_READY;

	if (dst == NULL)
		dst = (uint8_t *) src;
#ifdef __x86_64__
	if (h->have_avx2)
		descramble2 (dst, src,
			h->ecm_kinfo[ecm_pid]->wrk[ca_flags == 0x02], (u32) len);
	else
#endif
		descramble (src, len, dst,
			h->ecm_kinfo[ecm_pid]->wrk[ca_flags == 0x02],
			h->init_param.round, h->init_param.cbc_init.whole);

	return DEMULTI2_RET_OK;
}

int
demulti2_feed_ecm (Demulti2Handle h, const uint8_t * buf, int len,
		uint16_t ecm_pid)
{
	struct demulti2_key_info *kinfo;
	union {
		uint8_t buf[16];
		uint64_t keys[2];
	} outbuf;
	int ret;
	m2key_t k_odd, k_even;

	if (!h)
		return DEMULTI2_E_INV_HANDLE;
	if (!buf || ecm_pid >= MAX_PID || ecm_pid == 0)
		return DEMULTI2_E_INV_ARG;

	kinfo = h->ecm_kinfo[ecm_pid];
	if (!kinfo)
		kinfo = h->ecm_kinfo[ecm_pid] = calloc (1, sizeof (*kinfo));
	if (!kinfo)
		return DEMULTI2_E_MEM;

#if CONFIG_YAKISOBA
	if (h->mode == MODE_YAKISOBA) {
		ret = (*bcas_decodeECM) (buf, len, outbuf.buf, NULL);
		if (ret != DEMULTI2_RET_OK) {
			if (kinfo->status != KEY_S_EMPTY)
				kinfo->status = KEY_S_OLD;
			return DEMULTI2_E_INV_ARG;
		}
	} else
#endif
#if (CONFIG_SOBACAS || CONFIG_PCSC)
	if (h->mode == MODE_PCSC) {
		int ret;
		if (!h->card)
			return DEMULTI2_E_NO_CARD;
		ret = bcas_send_ecm (h->card, buf, len, outbuf.buf);
		if (ret != DEMULTI2_RET_OK) {
			if (ret == DEMULTI2_E_NO_CARD)
				kinfo->status = KEY_S_EMPTY;
			else if (kinfo->status == KEY_S_OK)
				kinfo->status = KEY_S_OLD;
			else
				kinfo->status = KEY_S_EMPTY;
			return ret;
		}
	} else
#endif
		return DEMULTI2_E_NO_CARD; /* should not be reached */

	k_odd.whole = be64toh (outbuf.keys[0]);
	k_even.whole = be64toh (outbuf.keys[1]);

	if (k_odd.whole != kinfo->k_scr[0].whole) {
		kinfo->k_scr[0].whole = k_odd.whole;
		core_schedule (kinfo->wrk[0], h->init_param.k_sys, &k_odd.sub);
	}
	if (k_even.whole != kinfo->k_scr[1].whole) {
		kinfo->k_scr[1].whole = k_even.whole;
		core_schedule (kinfo->wrk[1], h->init_param.k_sys, &k_even.sub);
	}
	kinfo->status = KEY_S_OK;
	return DEMULTI2_RET_OK;
}

