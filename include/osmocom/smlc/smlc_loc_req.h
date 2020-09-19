/* Handle LCS BSSMAP-LE Perform Location Request */
/*
 * (C) 2020 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
 * All Rights Reserved
 *
 * Author: Neels Hofmeyr <neels@hofmeyr.de>
 *
 * SPDX-License-Identifier: GPL-2.0+
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#pragma once

#include <osmocom/smlc/debug.h>
#include <osmocom/gsm/bssmap_le.h>

#define LOG_SMLC_LOC_REQ(LOC_REQ, level, fmt, args...) do { \
		if (LOC_REQ) \
			LOGPFSML(LOC_REQ->fi, level, fmt, ## args); \
		else \
			LOGP(DLCS, level, "LCS Perf Loc Req: " fmt, ## args); \
	} while(0)

struct smlc_ta_req;
struct lb_conn;
struct msgb;

#define LB_CONN_USE_SMLC_LOC_REQ "smlc_loc_req"

enum smlc_loc_req_fsm_event {
	SMLC_LOC_REQ_EV_RX_TA_RESPONSE,
	SMLC_LOC_REQ_EV_RX_BSSLAP_RESET,
	SMLC_LOC_REQ_EV_RX_LE_PERFORM_LOCATION_ABORT,
};

struct smlc_loc_req {
	struct osmo_fsm_inst *fi;

	struct smlc_subscr *smlc_subscr;
	struct lb_conn *lb_conn;

	struct bssmap_le_perform_loc_req req;

	bool ta_present;
	uint8_t ta;

	struct gsm0808_cell_id latest_cell_id;

	struct lcs_cause_ie lcs_cause;
};

int smlc_loc_req_rx_bssap_le(struct lb_conn *conn, const struct bssap_le_pdu *bssap_le);
