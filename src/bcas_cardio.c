/*
 * bcas_cardio.c - PC/SC card I/F for BCAS descrambling
 * Copyright (C) 2013 tsunoda14
 *
 * Authors:
 *	 tsunoda14
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <winscard.h> /* for API prototyping */

/* dummy objects for winscard.h. to use it w/o linking to libpcsclite */
const SCARD_IO_REQUEST g_rgSCardT0Pci = { 0 };
const SCARD_IO_REQUEST g_rgSCardT1Pci = { 0 };
const SCARD_IO_REQUEST g_rgSCardRawPci = { 0 };

/* for error code */
#include "demulti2.h"
#include "bcas_cardio.h"

#if HAVE_ENDIAN_H
#include <endian.h>
#elif HAVE_SYS_ENDIAN_H
#include <sys/endian.h>
#endif

#if HAVE_BETOH64
#define be32toh(x) betoh32((x))
#define be64toh(x) betoh64((x))
#endif


#define BCAS_MAX_CARDID 8
#define BCAS_MAX_MSG_LEN 266
#define BCAS_MAX_IO_ERROR 3

#define ECM_RSP_UNIT_SIZE 21

#define CARD_SW1_OK 0x90
#define CARD_SW2_OK 0x00

#define CARD_TYPE_GENERAL 0x01

#define SYS_MNG_ID_B14 0x0301
#define SYS_MNG_ID_B15 0x0201

#define CA_SYSTEM_ID_ARIB 0x0005
#define CA_SYSTEM_ID_ARIB_B 0x000A

#define CARD_RETCODE_OK 0x2100
#define CARD_RETCODE_GD_TIER 0x0800
#define CARD_RETCODE_GD_PREPPV 0x0400
#define CARD_RETCODE_GD_POSTPPV 0x0200
#define CARD_RETCODE_PV_PREPPV 0x4480
#define CARD_RETCODE_PV_POSTPPV 0x4280

#define CARD_DIR_PWRON_CTL (1 << 0)
#define CARD_DIR_RINGING_DATE (1 << 1)
#define CARD_DIR_DEL_PASSWD (1 << 2)
#define CARD_DIR_RING (1 << 3)
#define CARD_DIR_RETRY_OVER (1 << 5)
#define CARD_DIR_HANGUP_LINE (1 << 6)
#define CARD_DIR_INIT_PARAM (1 << 7)
#define CARD_DIR_CARD_ID (1 << 8)
#define CARD_DIR_CHANGE_CARD (1 << 9)

/*
 * for dlsym
 */
#define DEF_DL(func) typeof(&(func)) p##func
DEF_DL(SCardEstablishContext);
DEF_DL(SCardReleaseContext);
DEF_DL(SCardConnect);
DEF_DL(SCardReconnect);
DEF_DL(SCardDisconnect);
DEF_DL(SCardTransmit);
DEF_DL(SCardStatus);
DEF_DL(SCardListReaders);
typeof(&g_rgSCardT1Pci) pSCARD_PCI_T1;

struct _bcas_card {
	const char *iccname;
	uint16_t cas_id;
	enum {CARD_S_NG, CARD_S_OK} status;
	int num_id;
	uint8_t id[BCAS_MAX_CARDID][10];
	uint64_t cbc_init;
	uint32_t k_sys[8];

	/* PC/SC card handles etc. used by bcas_card_*() */
	SCARDCONTEXT cxt;
	SCARDHANDLE handle;

	int error_count;
};


/* example ATR of BCAS. note: byte[2],[4],[8-9],[11-12] may vary */
static const BYTE bcas_atr[] = {
	0x3B, 0xF0, 0x12, 0x00, 0xFF, 0x91, 0x81,
	0xB1, 0x7C, 0x45, 0x1F, 0x03, 0x99
};

static const DWORD BCAS_ATR_LEN = sizeof (bcas_atr);

static const char *
get_card (SCARDCONTEXT cxt, SCARDHANDLE * h, const char * iccname)
{
	LONG ret;
	DWORD len, protocol;
	char *p, *readers;
	DWORD nlen, alen;
	DWORD state;
	BYTE atr[MAX_ATR_SIZE];

	ret = (*pSCardListReaders) (cxt, NULL, NULL, &len);
	if (ret != SCARD_S_SUCCESS)
		return NULL;

	readers = (char *) malloc (len);
	if (!readers)
		return NULL;

	ret = (*pSCardListReaders) (cxt, NULL, readers, &len);
	if (ret != SCARD_S_SUCCESS) {
		free (readers);
		return NULL;
	}

	for (p = readers; *p; p += strlen (p) + 1) {
		if (iccname && strcmp (p, iccname))
			continue;
		ret = (*pSCardConnect) (cxt, p, SCARD_SHARE_SHARED,
		        SCARD_PROTOCOL_T1, h, &protocol);
		if (ret != SCARD_S_SUCCESS)
			continue;
		alen = sizeof (atr);
		ret = (*pSCardStatus) (*h, NULL, &nlen, &state, &protocol, atr, &alen);
		if (ret == SCARD_S_SUCCESS && (state & SCARD_PRESENT) &&
				alen == BCAS_ATR_LEN &&
				atr[0] == bcas_atr[0] && atr[1] == bcas_atr[1]) {
			if (!iccname)
				iccname = strdup (p);
			free (readers);
			return iccname;
		}
	}
	free (readers);
	return NULL;
}

static LONG
send_cmd (bcas_card_t * card, const uint8_t *cmd, DWORD len,
		uint8_t *rspbuf, DWORD * rlen, int retry)
{
	LONG ret, ret2;
	DWORD orig_rlen;
	DWORD protocol;
	SCARD_IO_REQUEST rpci;

	orig_rlen = *rlen;
	memcpy (&rpci, pSCARD_PCI_T1, sizeof (rpci));
	do {
		*rlen = orig_rlen;
		ret = (*pSCardTransmit) (card->handle, pSCARD_PCI_T1, cmd, len,
			&rpci, rspbuf, rlen);

		if (ret == SCARD_W_RESET_CARD) {
			ret = (*pSCardReconnect) (card->handle, SCARD_SHARE_SHARED,
				SCARD_PROTOCOL_T1, SCARD_RESET_CARD, &protocol);
			if (ret == SCARD_S_SUCCESS) {
				*rlen = orig_rlen;
				ret = (*pSCardTransmit) (card->handle, pSCARD_PCI_T1, cmd, len,
					&rpci, rspbuf, rlen);
			}
		}
		if (ret == SCARD_S_SUCCESS) {
				card->error_count = 0;
				return ret;
		}

		if (ret != SCARD_W_RESET_CARD)
			card->error_count ++;
		if (card->error_count >= BCAS_MAX_IO_ERROR) {
			card->status = CARD_S_NG;
			return ret;
		}

		(*pSCardDisconnect) (card->handle, SCARD_RESET_CARD);
		sleep (1);
		ret2 = (*pSCardConnect) (card->cxt, card->iccname, SCARD_SHARE_SHARED,
			SCARD_PROTOCOL_T1, &card->handle, &protocol);
		if (ret2 != SCARD_S_SUCCESS) {
			card->status = CARD_S_NG;
			return ret;
		}
	} while (retry-- > 0);
	return ret;
}

 
static int
setup_card (bcas_card_t * card)
{
	LONG ret;
	DWORD rlen;
	uint8_t rspbuf[BCAS_MAX_MSG_LEN];

	const char *name;
	uint16_t cas_id;
	int i;

	static const uint8_t init_cmd[] = { 0x90, 0x30, 0x00, 0x00, 0x00 };
	static const uint8_t info_cmd[] = { 0x90, 0x32, 0x00, 0x00, 0x00 };

	/* init card and set some ID infos */
	ret = (*pSCardEstablishContext) (SCARD_SCOPE_SYSTEM, NULL, NULL, &card->cxt);
	if (ret != SCARD_S_SUCCESS)
		return -1;
	// SCardSetTimeout (card->cxt, 3);

	name = get_card (card->cxt, &card->handle, getenv("DEMULTI2_CARD"));
	if (!name)
		goto bailout;

	/* discard the old name string if any */
	if (card->iccname)
		free (card->iccname);
	card->iccname = name;

	rlen = sizeof (rspbuf);
	ret = send_cmd (card, init_cmd, sizeof (init_cmd), rspbuf, &rlen, 2);
	if (ret != SCARD_S_SUCCESS || rlen < 59)
		goto bailout;

	cas_id = rspbuf[6] << 8 | rspbuf[7];
	if (rspbuf[rlen - 2] != CARD_SW1_OK || rspbuf[rlen - 1] != CARD_SW2_OK ||
			rspbuf[1] < 55 || (rspbuf[4] << 8 | rspbuf[5]) != CARD_RETCODE_OK ||
			(cas_id != CA_SYSTEM_ID_ARIB && cas_id != CA_SYSTEM_ID_ARIB_B) ||
			rspbuf[14] != CARD_TYPE_GENERAL)
		goto bailout;

	card->cas_id = cas_id;

	/* TODO: check the card directions(rspbuf[2-3]) and retcode(rspbuf[4-5]) */
	/* TODO: check if sys_management_id[](rspbuf[57..]) includes the valid ID */

	/* copy the initial data */
	for (i = 0; i < 8; i++) {
		card->k_sys[i] = be32toh (*(uint32_t *) (rspbuf + 16 + i * 4));
	}
	card->cbc_init = be64toh (*((uint64_t *) & rspbuf[48]));

	/* redundant. overwritten very soon by the next command */
	memcpy (&card->id[0][2], &rspbuf[8], 6);
	card->num_id = 1;

	/* get the card info */
	rlen = sizeof (rspbuf);
	ret = send_cmd (card, info_cmd, sizeof (info_cmd), rspbuf, &rlen, 2);
	if (ret != SCARD_S_SUCCESS || rlen < 19)
		goto bailout;

	if (rspbuf[rlen - 2] != CARD_SW1_OK || rspbuf[rlen - 1] != CARD_SW2_OK ||
			rspbuf[1] < 15 || (rspbuf[4] << 8 | rspbuf[5]) != CARD_RETCODE_OK ||
			rspbuf[6] < 1 || rspbuf[6] > 8)
		goto bailout;

	/* copy the card IDs */
	card->num_id = rspbuf[6];
	memcpy (card->id, &rspbuf[7], 10 * rspbuf[6]);
	card->status = CARD_S_OK;
	return 0;

bailout:
	if (card->handle)
		(*pSCardDisconnect) (card->handle, SCARD_LEAVE_CARD);
	(*pSCardReleaseContext) (card->cxt);
	return -1;
}

int
bcas_send_ecm(bcas_card_t *card, const uint8_t *buf, int len, uint8_t *out)
{
	static uint8_t ecm_cmd[BCAS_MAX_MSG_LEN] = { 0x90, 0x34, 0x00, 0x00, };
	uint8_t rspbuf[BCAS_MAX_MSG_LEN];
	LONG ret;
	DWORD rlen;
	uint16_t retcode;

	if (!card->handle || card->status != CARD_S_OK)
		return DEMULTI2_E_NO_CARD;

	/* assert(out != NULL); */
	if (len + 6 > sizeof (rspbuf))
		return DEMULTI2_E_INV_ARG;

	ecm_cmd[4] = len;
	memcpy(ecm_cmd + 5, buf, len);
	ecm_cmd[5 + len] = 0x00;
	rlen = sizeof (rspbuf);
	ret = send_cmd (card, ecm_cmd, len + 6, rspbuf, &rlen, 0); 

	if (ret != SCARD_S_SUCCESS)
		return DEMULTI2_E_CARD_IO;

	if (rlen < ECM_RSP_UNIT_SIZE + 4 || rspbuf[1] < ECM_RSP_UNIT_SIZE
	    || rspbuf[rlen - 2] != CARD_SW1_OK || rspbuf[rlen - 1] != CARD_SW2_OK)
		return DEMULTI2_E_BAD_RESP;

	if (rspbuf[2] || rspbuf[3]) {
		uint16_t card_dir;

		card_dir = rspbuf[2] << 8 | rspbuf[3];
		/* TODO: report the directions from IC cards. */
	}

	retcode = rspbuf[4] << 8 | rspbuf[5];
	if (retcode != CARD_RETCODE_GD_TIER &&
			retcode != CARD_RETCODE_GD_PREPPV &&
			retcode != CARD_RETCODE_GD_POSTPPV &&
			retcode != CARD_RETCODE_PV_PREPPV && retcode != CARD_RETCODE_PV_POSTPPV)
		return DEMULTI2_E_BAD_RESP;

	memcpy(out, rspbuf + 6, 16);
	return DEMULTI2_RET_OK;
}


bcas_card_t *
bcas_card_init (void *lib)
{
	bcas_card_t *card;

	/* resolve required symbos */
	pSCardEstablishContext = dlsym(lib, "SCardEstablishContext");
	pSCardReleaseContext = dlsym(lib, "SCardReleaseContext");
	pSCardConnect = dlsym(lib, "SCardConnect");
	pSCardReconnect = dlsym(lib, "SCardReconnect");
	pSCardDisconnect = dlsym(lib, "SCardDisconnect");
	pSCardTransmit = dlsym(lib, "SCardTransmit");
	pSCardStatus = dlsym(lib, "SCardStatus");
	pSCardListReaders = dlsym(lib, "SCardListReaders");
	pSCARD_PCI_T1 = dlsym(lib, "g_rgSCardT1Pci");
	if (!pSCardEstablishContext || !pSCardReleaseContext \
			|| !pSCardConnect || !pSCardReconnect || !pSCardDisconnect \
			|| !SCardTransmit || !pSCardStatus || !pSCardListReaders \
			|| !pSCARD_PCI_T1)
		return NULL;

	card = (bcas_card_t *) calloc(1, sizeof(bcas_card_t));
	if (card == NULL)
		return NULL;
	/* assert(card->status == CARD_S_NG), as card is calloc'ed. */

	if (setup_card (card)) {
		free (card);
		return NULL;
	}

	return card;
}


void
bcas_card_stop (bcas_card_t * card)
{
	if (card == NULL)
		return;

	if (card->handle)
		(*pSCardDisconnect) (card->handle, SCARD_LEAVE_CARD);
	(*pSCardReleaseContext) (card->cxt);
	free (card->iccname);
	free(card);
}
