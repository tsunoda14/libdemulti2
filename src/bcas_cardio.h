/*
 *  MULTI2 Descrambler for BCAS
 *
 *  Copyright (C) 2009 tsunoda14
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
#ifndef _BCAS_CARDIO_H_
#define _BCAS_CARDIO_H_

#include <inttypes.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct _bcas_card bcas_card_t;

extern bcas_card_t *bcas_card_init(void * lib);
extern void bcas_card_stop(bcas_card_t * card);
extern int bcas_send_ecm(bcas_card_t * card, const uint8_t * buf, int rlen,
	uint8_t * out);

#ifdef __cplusplus
}
#endif
#endif /* _BCAS_CARDIO_H_ */
