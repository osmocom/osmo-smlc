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
 */

#include <osmocom/smlc/smlc_data.h>
#include <osmocom/smlc/smlc_loc_req.h>
#include <osmocom/smlc/smlc_subscr.h>
#include <osmocom/smlc/lb_conn.h>
#include <osmocom/smlc/cell_locations.h>

#include <osmocom/core/fsm.h>
#include <osmocom/core/tdef.h>
#include <osmocom/core/utils.h>
#include <osmocom/gsm/bsslap.h>
#include <osmocom/gsm/bssmap_le.h>
#include <osmocom/gsm/gad.h>

enum smlc_loc_req_fsm_state {
	SMLC_LOC_REQ_ST_INIT,
	SMLC_LOC_REQ_ST_WAIT_TA,
	SMLC_LOC_REQ_ST_GOT_TA,
	SMLC_LOC_REQ_ST_FAILED,
};

static const struct value_string smlc_loc_req_fsm_event_names[] = {
	OSMO_VALUE_STRING(SMLC_LOC_REQ_EV_RX_TA_RESPONSE),
	OSMO_VALUE_STRING(SMLC_LOC_REQ_EV_RX_BSSLAP_RESET),
	OSMO_VALUE_STRING(SMLC_LOC_REQ_EV_RX_LE_PERFORM_LOCATION_ABORT),
	{}
};

static struct osmo_fsm smlc_loc_req_fsm;

static const struct osmo_tdef_state_timeout smlc_loc_req_fsm_timeouts[32] = {
	[SMLC_LOC_REQ_ST_WAIT_TA] = { .T = -12 },
};

/* Transition to a state, using the T timer defined in smlc_loc_req_fsm_timeouts.
 * The actual timeout value is in turn obtained from network->T_defs.
 * Assumes local variable fi exists. */
#define smlc_loc_req_fsm_state_chg(FI, STATE) \
	osmo_tdef_fsm_inst_state_chg(FI, STATE, \
				     smlc_loc_req_fsm_timeouts, \
				     g_smlc_tdefs, \
				     5)

#define smlc_loc_req_fail(cause, fmt, args...) do { \
		LOG_SMLC_LOC_REQ(smlc_loc_req, LOGL_ERROR, "Perform Location Request failed in state %s: " fmt "\n", \
				 smlc_loc_req ? osmo_fsm_inst_state_name(smlc_loc_req->fi) : "NULL", ## args); \
		smlc_loc_req->lcs_cause = (struct lcs_cause_ie){ \
			.present = true, \
			.cause_val = cause, \
		}; \
		smlc_loc_req_fsm_state_chg(smlc_loc_req->fi, SMLC_LOC_REQ_ST_FAILED); \
	} while(0)

static struct smlc_loc_req *smlc_loc_req_alloc(void *ctx)
{
	struct smlc_loc_req *smlc_loc_req;

	struct osmo_fsm_inst *fi = osmo_fsm_inst_alloc(&smlc_loc_req_fsm, ctx, NULL, LOGL_DEBUG, "no-id");
	OSMO_ASSERT(fi);

	smlc_loc_req = talloc(fi, struct smlc_loc_req);
	OSMO_ASSERT(smlc_loc_req);
	fi->priv = smlc_loc_req;
	*smlc_loc_req = (struct smlc_loc_req){
		.fi = fi,
	};

	return smlc_loc_req;
}

static int smlc_loc_req_start(struct lb_conn *lb_conn, const struct bssmap_le_perform_loc_req *loc_req_pdu)
{
	struct smlc_loc_req *smlc_loc_req;

	rate_ctr_inc(rate_ctr_group_get_ctr(g_smlc->ctrs, SMLC_CTR_BSSMAP_LE_RX_DT1_PERFORM_LOCATION_REQUEST));

	if (lb_conn->smlc_loc_req) {
		/* Another request is already pending. If we send Perform Location Abort, the peer doesn't know which
		 * request we would mean. Just drop this on the floor. */
		LOG_SMLC_LOC_REQ(lb_conn->smlc_loc_req, LOGL_ERROR,
				"Ignoring Perform Location Request, another request is still pending\n");
		return -EAGAIN;
	}

	if (loc_req_pdu->imsi.type == GSM_MI_TYPE_IMSI
	    && (!lb_conn->smlc_subscr
		|| osmo_mobile_identity_cmp(&loc_req_pdu->imsi, &lb_conn->smlc_subscr->imsi))) {

		struct smlc_subscr *smlc_subscr;
		struct lb_conn *other_conn;
		smlc_subscr = smlc_subscr_find_or_create(&loc_req_pdu->imsi, __func__);
		OSMO_ASSERT(smlc_subscr);

		if (lb_conn->smlc_subscr && lb_conn->smlc_subscr != smlc_subscr) {
			LOG_LB_CONN(lb_conn, LOGL_ERROR,
				    "IMSI mismatch: lb_conn has %s, Rx Perform Location Request has %s\n",
				    smlc_subscr_to_str_c(OTC_SELECT, lb_conn->smlc_subscr),
				    smlc_subscr_to_str_c(OTC_SELECT, smlc_subscr));
			smlc_subscr_put(smlc_subscr, __func__);
			return -EINVAL;
		}

		/* Find another conn before setting this conn's subscriber */
		other_conn = lb_conn_find_by_smlc_subscr(lb_conn->smlc_subscr, __func__);

		/* Set the subscriber before logging about it, so that it shows as log context */
		if (!lb_conn->smlc_subscr) {
			lb_conn->smlc_subscr = smlc_subscr;
			smlc_subscr_get(lb_conn->smlc_subscr, SMLC_SUBSCR_USE_LB_CONN);
		}

		if (other_conn && other_conn != lb_conn) {
			LOG_LB_CONN(lb_conn, LOGL_ERROR, "Another conn already active for this subscriber\n");
			LOG_LB_CONN(other_conn, LOGL_ERROR, "Another conn opened for this subscriber, discarding\n");
			lb_conn_close(other_conn);
		}

		smlc_subscr_put(smlc_subscr, __func__);
		if (other_conn)
			lb_conn_put(other_conn, __func__);
	}

	/* smlc_loc_req has a use count on lb_conn, so its talloc ctx must not be a child of lb_conn. (Otherwise an
	 * lb_conn_put() from smlc_loc_req could cause a free of smlc_loc_req's parent ctx, causing a use after free on
	 * FSM termination.) */
	smlc_loc_req = smlc_loc_req_alloc(lb_conn->lb_peer);

	*smlc_loc_req = (struct smlc_loc_req){
		.fi = smlc_loc_req->fi,
		.lb_conn = lb_conn,
		.req = *loc_req_pdu,
	};
	smlc_loc_req->latest_cell_id = loc_req_pdu->cell_id;
	lb_conn->smlc_loc_req = smlc_loc_req;
	lb_conn_get(smlc_loc_req->lb_conn, LB_CONN_USE_SMLC_LOC_REQ);

	LOG_LB_CONN(lb_conn, LOGL_INFO, "Rx Perform Location Request (BSSLAP APDU %s), cell id is %s\n",
		    loc_req_pdu->apdu_present ?
		    osmo_bsslap_msgt_name(loc_req_pdu->apdu.msg_type) : "omitted",
		    gsm0808_cell_id_name_c(OTC_SELECT, &smlc_loc_req->latest_cell_id));

	/* state change to start the timeout */
	smlc_loc_req_fsm_state_chg(smlc_loc_req->fi, SMLC_LOC_REQ_ST_WAIT_TA);
	return 0;
}

static int handle_bssmap_le_conn_oriented_info(struct smlc_loc_req *smlc_loc_req,
					       const struct bssmap_le_conn_oriented_info *coi)
{
	switch (coi->apdu.msg_type) {

	case BSSLAP_MSGT_TA_RESPONSE:
		return osmo_fsm_inst_dispatch(smlc_loc_req->fi, SMLC_LOC_REQ_EV_RX_TA_RESPONSE,
					      (void*)&coi->apdu.ta_response);

	case BSSLAP_MSGT_RESET:
		return osmo_fsm_inst_dispatch(smlc_loc_req->fi, SMLC_LOC_REQ_EV_RX_BSSLAP_RESET,
					      (void*)&coi->apdu.reset);

	case BSSLAP_MSGT_ABORT:
		smlc_loc_req_fail(LCS_CAUSE_REQUEST_ABORTED, "Aborting Location Request due to BSSLAP Abort");
		return 0;

	case BSSLAP_MSGT_REJECT:
		smlc_loc_req_fail(LCS_CAUSE_REQUEST_ABORTED, "Aborting Location Request due to BSSLAP Reject");
		return 0;

	default:
		LOG_SMLC_LOC_REQ(smlc_loc_req, LOGL_ERROR, "rx BSSLAP APDU with unsupported message type %s\n",
				 osmo_bsslap_msgt_name(coi->apdu.msg_type));
		return -ENOTSUP;
	};
}

int smlc_loc_req_rx_bssap_le(struct lb_conn *lb_conn, const struct bssap_le_pdu *bssap_le)
{
	struct smlc_loc_req *smlc_loc_req = lb_conn->smlc_loc_req;
	const struct bssmap_le_pdu *bssmap_le = &bssap_le->bssmap_le;

	LOG_LB_CONN(lb_conn, LOGL_DEBUG, "Rx %s\n", osmo_bssap_le_pdu_to_str_c(OTC_SELECT, bssap_le));

	if (bssap_le->discr != BSSAP_LE_MSG_DISCR_BSSMAP_LE) {
		LOG_LB_CONN(lb_conn, LOGL_ERROR, "BSSAP-LE discr %d not implemented\n", bssap_le->discr);
		return -ENOTSUP;
	}

	switch (bssmap_le->msg_type) {

	case BSSMAP_LE_MSGT_PERFORM_LOC_REQ:
		return smlc_loc_req_start(lb_conn, &bssmap_le->perform_loc_req);

	case BSSMAP_LE_MSGT_PERFORM_LOC_ABORT:
		return osmo_fsm_inst_dispatch(smlc_loc_req->fi, SMLC_LOC_REQ_EV_RX_LE_PERFORM_LOCATION_ABORT,
					      (void*)&bssmap_le->perform_loc_abort);

	case BSSMAP_LE_MSGT_CONN_ORIENTED_INFO:
		return handle_bssmap_le_conn_oriented_info(smlc_loc_req, &bssmap_le->conn_oriented_info);

	default:
		LOG_SMLC_LOC_REQ(smlc_loc_req, LOGL_ERROR, "Rx BSSMAP-LE from SMLC with unsupported message type: %s\n",
				osmo_bssap_le_pdu_to_str_c(OTC_SELECT, bssap_le));
		return -ENOTSUP;
	}
}

void smlc_loc_req_reset(struct lb_conn *lb_conn)
{
	struct smlc_loc_req *smlc_loc_req = lb_conn->smlc_loc_req;
	if (!smlc_loc_req)
		return;
	smlc_loc_req_fail(LCS_CAUSE_SYSTEM_FAILURE, "Aborting Location Request due to RESET on Lb");
}

static int smlc_loc_req_fsm_timer_cb(struct osmo_fsm_inst *fi)
{
	struct smlc_loc_req *smlc_loc_req = fi->priv;
	smlc_loc_req_fail(LCS_CAUSE_SYSTEM_FAILURE, "Timeout");
	return 1;
}

static void smlc_loc_req_wait_ta_onenter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct smlc_loc_req *smlc_loc_req = fi->priv;
	struct bssmap_le_pdu bssmap_le;

	/* Did the original request contain a TA already? */
	if (smlc_loc_req->req.apdu_present && smlc_loc_req->req.apdu.msg_type == BSSLAP_MSGT_TA_LAYER3) {
		smlc_loc_req->ta_present = true;
		smlc_loc_req->ta = smlc_loc_req->req.apdu.ta_layer3.ta;
		LOG_SMLC_LOC_REQ(smlc_loc_req, LOGL_INFO, "TA = %u\n", smlc_loc_req->ta);
		smlc_loc_req_fsm_state_chg(smlc_loc_req->fi, SMLC_LOC_REQ_ST_GOT_TA);
		return;
	}

	/* No TA known yet, ask via BSSLAP */
	bssmap_le = (struct bssmap_le_pdu){
		.msg_type = BSSMAP_LE_MSGT_CONN_ORIENTED_INFO,
		.conn_oriented_info = {
			.apdu = {
				.msg_type = BSSLAP_MSGT_TA_REQUEST,
			},
		},
	};

	lb_conn_send_bssmap_le(smlc_loc_req->lb_conn, &bssmap_le);
}

static void update_ci(struct gsm0808_cell_id *cell_id, int16_t new_ci)
{
	struct osmo_cell_global_id cgi = {};
	struct gsm0808_cell_id ci = {
		.id_discr = CELL_IDENT_CI,
		.id.ci = new_ci,
	};
	/* Set all values from the cell_id to the cgi */
	gsm0808_cell_id_to_cgi(&cgi, cell_id);
	/* Overwrite the CI part */
	gsm0808_cell_id_to_cgi(&cgi, &ci);
	/* write back to cell_id, without changing its type */
	gsm0808_cell_id_from_cgi(cell_id, cell_id->id_discr, &cgi);
}

static void smlc_loc_req_wait_ta_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct smlc_loc_req *smlc_loc_req = fi->priv;
	const struct bsslap_ta_response *ta_response;
	const struct bsslap_reset *reset;

	switch (event) {

	case SMLC_LOC_REQ_EV_RX_TA_RESPONSE:
		ta_response = data;
		smlc_loc_req->ta_present = true;
		smlc_loc_req->ta = ta_response->ta;
		update_ci(&smlc_loc_req->latest_cell_id, ta_response->cell_id);
		LOG_SMLC_LOC_REQ(smlc_loc_req, LOGL_INFO, "Rx BSSLAP TA Response: cell id is now %s\n",
				 gsm0808_cell_id_name_c(OTC_SELECT, &smlc_loc_req->latest_cell_id));
		smlc_loc_req_fsm_state_chg(smlc_loc_req->fi, SMLC_LOC_REQ_ST_GOT_TA);
		return;

	case SMLC_LOC_REQ_EV_RX_BSSLAP_RESET:
		reset = data;
		smlc_loc_req->ta_present = true;
		smlc_loc_req->ta = reset->ta;
		update_ci(&smlc_loc_req->latest_cell_id, reset->cell_id);
		LOG_SMLC_LOC_REQ(smlc_loc_req, LOGL_INFO, "Rx BSSLAP Reset: cell id is now %s\n",
				 gsm0808_cell_id_name_c(OTC_SELECT, &smlc_loc_req->latest_cell_id));
		smlc_loc_req_fsm_state_chg(smlc_loc_req->fi, SMLC_LOC_REQ_ST_GOT_TA);
		return;

	case SMLC_LOC_REQ_EV_RX_LE_PERFORM_LOCATION_ABORT:
		LOG_SMLC_LOC_REQ(smlc_loc_req, LOGL_INFO, "Rx Perform Location Abort, stopping this request dead\n");
		osmo_fsm_inst_term(fi, OSMO_FSM_TERM_REQUEST, NULL);
		return;

	default:
		OSMO_ASSERT(false);
	}
}

static void smlc_loc_req_got_ta_onenter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct smlc_loc_req *smlc_loc_req = fi->priv;
	struct bssmap_le_pdu bssmap_le;
	struct osmo_gad location;
	int rc;

	if (!smlc_loc_req->ta_present) {
		smlc_loc_req_fail(LCS_CAUSE_SYSTEM_FAILURE,
				  "Internal error: GOT_TA event, but no TA present");
		return;
	}

	bssmap_le = (struct bssmap_le_pdu){
		.msg_type = BSSMAP_LE_MSGT_PERFORM_LOC_RESP,
		.perform_loc_resp = {
			.location_estimate_present = true,
		},
	};

	rc = cell_location_from_ta(&location, &smlc_loc_req->latest_cell_id, smlc_loc_req->ta);
	if (rc) {
		smlc_loc_req_fail(LCS_CAUSE_FACILITY_NOTSUPP, "Unable to compose Location Estimate for %s: %s",
				  gsm0808_cell_id_name_c(OTC_SELECT, &smlc_loc_req->latest_cell_id),
				  rc == -ENOENT ? "No location information for this cell" : "unknown error");
		return;
	}

	rc = osmo_gad_enc(&bssmap_le.perform_loc_resp.location_estimate, &location);
	if (rc <= 0) {
		smlc_loc_req_fail(LCS_CAUSE_FACILITY_NOTSUPP, "Unable to encode Location Estimate for %s (rc=%d)",
				  gsm0808_cell_id_name_c(OTC_SELECT, &smlc_loc_req->latest_cell_id), rc);
		return;
	}

	LOG_SMLC_LOC_REQ(smlc_loc_req, LOGL_INFO, "Returning location estimate to BSC: %s TA=%u --> %s\n",
			 gsm0808_cell_id_name_c(OTC_SELECT, &smlc_loc_req->latest_cell_id),
			 smlc_loc_req->ta, osmo_gad_to_str_c(OTC_SELECT, &location));

	if (lb_conn_send_bssmap_le(smlc_loc_req->lb_conn, &bssmap_le)) {
		smlc_loc_req_fail(LCS_CAUSE_SYSTEM_FAILURE,
				  "Unable to encode/send BSSMAP-LE Perform Location Response");
		return;
	}
	osmo_fsm_inst_term(fi, OSMO_FSM_TERM_REGULAR, NULL);
}

static void smlc_loc_req_failed_onenter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct smlc_loc_req *smlc_loc_req = fi->priv;
	struct bssmap_le_pdu bssmap_le = {
		.msg_type = BSSMAP_LE_MSGT_PERFORM_LOC_RESP,
		.perform_loc_resp = {
			.lcs_cause = smlc_loc_req->lcs_cause,
		},
	};
	int rc;
	rc = lb_conn_send_bssmap_le(smlc_loc_req->lb_conn, &bssmap_le);
	osmo_fsm_inst_term(fi, rc ? OSMO_FSM_TERM_ERROR : OSMO_FSM_TERM_REGULAR, NULL);
}

void smlc_loc_req_fsm_cleanup(struct osmo_fsm_inst *fi, enum osmo_fsm_term_cause cause)
{
	struct smlc_loc_req *smlc_loc_req = fi->priv;
	if (smlc_loc_req->lb_conn && smlc_loc_req->lb_conn->smlc_loc_req == smlc_loc_req) {
		smlc_loc_req->lb_conn->smlc_loc_req = NULL;
		lb_conn_put(smlc_loc_req->lb_conn, LB_CONN_USE_SMLC_LOC_REQ);
	}
}

#define S(x)    (1 << (x))

static const struct osmo_fsm_state smlc_loc_req_fsm_states[] = {
	[SMLC_LOC_REQ_ST_INIT] = {
		.name = "INIT",
		.out_state_mask = 0
			| S(SMLC_LOC_REQ_ST_WAIT_TA)
			| S(SMLC_LOC_REQ_ST_FAILED)
			,
	},
	[SMLC_LOC_REQ_ST_WAIT_TA] = {
		.name = "WAIT_TA",
		.in_event_mask = 0
			| S(SMLC_LOC_REQ_EV_RX_TA_RESPONSE)
			| S(SMLC_LOC_REQ_EV_RX_BSSLAP_RESET)
			| S(SMLC_LOC_REQ_EV_RX_LE_PERFORM_LOCATION_ABORT)
			,
		.out_state_mask = 0
			| S(SMLC_LOC_REQ_ST_GOT_TA)
			| S(SMLC_LOC_REQ_ST_FAILED)
			,
		.onenter = smlc_loc_req_wait_ta_onenter,
		.action = smlc_loc_req_wait_ta_action,
	},
	[SMLC_LOC_REQ_ST_GOT_TA] = {
		.name = "GOT_TA",
		.out_state_mask = 0
			| S(SMLC_LOC_REQ_ST_FAILED)
			,
		.onenter = smlc_loc_req_got_ta_onenter,
	},
	[SMLC_LOC_REQ_ST_FAILED] = {
		.name = "FAILED",
		.onenter = smlc_loc_req_failed_onenter,
	},
};

static struct osmo_fsm smlc_loc_req_fsm = {
	.name = "smlc_loc_req",
	.states = smlc_loc_req_fsm_states,
	.num_states = ARRAY_SIZE(smlc_loc_req_fsm_states),
	.log_subsys = DLCS,
	.event_names = smlc_loc_req_fsm_event_names,
	.timer_cb = smlc_loc_req_fsm_timer_cb,
	.cleanup = smlc_loc_req_fsm_cleanup,
};

static __attribute__((constructor)) void smlc_loc_req_fsm_register(void)
{
	OSMO_ASSERT(osmo_fsm_register(&smlc_loc_req_fsm) == 0);
}
