/*
 * (C) 2019 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
 * All Rights Reserved
 *
 * SPDX-License-Identifier: AGPL-3.0+
 *
 * Author: Neels Hofmeyr
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/fsm.h>
#include <osmocom/gsm/bssmap_le.h>
#include <osmocom/sigtran/sccp_helpers.h>

#include <osmocom/smlc/smlc_data.h>
#include <osmocom/smlc/sccp_lb_inst.h>
#include <osmocom/smlc/lb_peer.h>

static struct osmo_fsm lb_peer_fsm;

static __attribute__((constructor)) void lb_peer_init()
{
	OSMO_ASSERT( osmo_fsm_register(&lb_peer_fsm) == 0);
}

static struct lb_peer *lb_peer_alloc(struct sccp_lb_inst *sli, const struct osmo_sccp_addr *peer_addr)
{
	struct lb_peer *lbp;
	struct osmo_fsm_inst *fi;

	fi = osmo_fsm_inst_alloc(&lb_peer_fsm, sli, NULL, LOGL_DEBUG, NULL);
	OSMO_ASSERT(fi);

	osmo_fsm_inst_update_id(fi, osmo_sccp_addr_to_id_c(OTC_SELECT, osmo_sccp_get_ss7(sli->sccp), peer_addr));

	lbp = talloc_zero(fi, struct lb_peer);
	OSMO_ASSERT(lbp);
	*lbp = (struct lb_peer){
		.fi = fi,
		.sli = sli,
		.peer_addr = *peer_addr,
	};
	fi->priv = lbp;

	llist_add(&lbp->entry, &sli->lb_peers);

	return lbp;
}

struct lb_peer *lb_peer_find_or_create(struct sccp_lb_inst *sli, const struct osmo_sccp_addr *peer_addr)
{
	struct lb_peer *lbp = lb_peer_find(sli, peer_addr);
	if (lbp)
		return lbp;
	return lb_peer_alloc(sli, peer_addr);
}

struct lb_peer *lb_peer_find(struct sccp_lb_inst *sli, const struct osmo_sccp_addr *peer_addr)
{
	struct lb_peer *lbp;
	llist_for_each_entry(lbp, &sli->lb_peers, entry) {
		if (osmo_sccp_addr_ri_cmp(peer_addr, &lbp->peer_addr))
			continue;
		return lbp;
	}
	return NULL;
}

static const struct osmo_tdef_state_timeout lb_peer_fsm_timeouts[32] = {
	[LB_PEER_ST_WAIT_RX_RESET_ACK] = { .T = -13 },
	[LB_PEER_ST_DISCARDING] = { .T = -14 },
};

#define lb_peer_state_chg(LB_PEER, NEXT_STATE) \
	osmo_tdef_fsm_inst_state_chg((LB_PEER)->fi, NEXT_STATE, lb_peer_fsm_timeouts, g_smlc_tdefs, 5)

void lb_peer_discard_all_conns(struct lb_peer *lbp)
{
	struct lb_conn *lb_conn, *next;

	lb_peer_for_each_lb_conn_safe(lb_conn, next, lbp) {
		lb_conn_discard(lb_conn);
	}
}

/* Drop all SCCP connections for this lb_peer, respond with RESET ACKNOWLEDGE and move to READY state. */
static void lb_peer_rx_reset(struct lb_peer *lbp, struct msgb *msg)
{
	struct msgb *resp;
	struct bssap_le_pdu reset_ack = {
		.discr = BSSAP_LE_MSG_DISCR_BSSMAP_LE,
		.bssmap_le = {
			.msg_type = BSSMAP_LE_MSGT_RESET_ACK,
		},
	};

	lb_peer_discard_all_conns(lbp);

	resp = osmo_bssap_le_enc(&reset_ack);
	if (!resp) {
		LOG_LB_PEER(lbp, LOGL_ERROR, "Failed to compose RESET ACKNOWLEDGE message\n");
		lb_peer_state_chg(lbp, LB_PEER_ST_WAIT_RX_RESET);
		return;
	}

	if (sccp_lb_down_l2_cl(lbp->sli, &lbp->peer_addr, resp)) {
		LOG_LB_PEER(lbp, LOGL_ERROR, "Failed to send RESET ACKNOWLEDGE message\n");
		lb_peer_state_chg(lbp, LB_PEER_ST_WAIT_RX_RESET);
		msgb_free(msg);
		return;
	}

	LOG_LB_PEER(lbp, LOGL_INFO, "Sent RESET ACKNOWLEDGE\n");

	/* sccp_lb_down_l2_cl() doesn't free msgb */
	msgb_free(resp);

	lb_peer_state_chg(lbp, LB_PEER_ST_READY);
}

static void lb_peer_rx_reset_ack(struct lb_peer *lbp, struct msgb* msg)
{
	lb_peer_state_chg(lbp, LB_PEER_ST_READY);
}

void lb_peer_reset(struct lb_peer *lbp)
{
	struct bssap_le_pdu reset = {
		.discr = BSSAP_LE_MSG_DISCR_BSSMAP_LE,
		.bssmap_le = {
			.msg_type = BSSMAP_LE_MSGT_RESET,
			.reset = GSM0808_CAUSE_EQUIPMENT_FAILURE,
		},
	};
	struct msgb *msg;
	int rc;

	lb_peer_state_chg(lbp, LB_PEER_ST_WAIT_RX_RESET_ACK);
	lb_peer_discard_all_conns(lbp);

	msg = osmo_bssap_le_enc(&reset);
	if (!msg) {
		LOG_LB_PEER(lbp, LOGL_ERROR, "Failed to compose RESET message\n");
		lb_peer_state_chg(lbp, LB_PEER_ST_WAIT_RX_RESET);
		return;
	}

	rc = sccp_lb_down_l2_cl(lbp->sli, &lbp->peer_addr, msg);
	msgb_free(msg);
	if (rc) {
		LOG_LB_PEER(lbp, LOGL_ERROR, "Failed to send RESET message\n");
		lb_peer_state_chg(lbp, LB_PEER_ST_WAIT_RX_RESET);
	}
}

void lb_peer_allstate_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct lb_peer *lbp = fi->priv;
	struct lb_peer_ev_ctx *ctx = data;
	struct msgb *msg = ctx->msg;
	enum bssmap_le_msgt msg_type;

	switch (event) {
	case LB_PEER_EV_MSG_UP_CL:
		msg_type = osmo_bssmap_le_msgt(msgb_l2(msg), msgb_l2len(msg));
		switch (msg_type) {
		case BSSMAP_LE_MSGT_RESET:
			osmo_fsm_inst_dispatch(fi, LB_PEER_EV_RX_RESET, msg);
			return;
		case BSSMAP_LE_MSGT_RESET_ACK:
			osmo_fsm_inst_dispatch(fi, LB_PEER_EV_RX_RESET_ACK, msg);
			return;
		default:
			LOG_LB_PEER(lbp, LOGL_ERROR, "Unhandled ConnectionLess message received: %s\n",
				    osmo_bssmap_le_msgt_name(msg_type));
			return;
		}

	default:
		LOG_LB_PEER(lbp, LOGL_ERROR, "Unhandled event: %s\n", osmo_fsm_event_name(&lb_peer_fsm, event));
		return;
	}
}

void lb_peer_st_wait_rx_reset(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct lb_peer *lbp = fi->priv;
	struct lb_peer_ev_ctx *ctx;
	struct msgb *msg;

	switch (event) {

	case LB_PEER_EV_MSG_UP_CO:
	case LB_PEER_EV_MSG_UP_CO_INITIAL:
		ctx = data;
		OSMO_ASSERT(ctx);
		LOG_LB_PEER(lbp, LOGL_ERROR, "Receiving CO message on Lb peer that has not done a proper RESET yet."
			     " Disconnecting on incoming message, sending RESET to Lb peer.\n");
		/* No valid RESET procedure has happened here yet. Usually, we're expecting the Lb peer (BSC,
		 * RNC) to first send a RESET message before sending Connection Oriented messages. So if we're
		 * getting a CO message, likely we've just restarted or something. Send a RESET to the peer. */

		lb_peer_disconnect(lbp->sli, ctx->conn_id);

		lb_peer_reset(lbp);
		return;

	case LB_PEER_EV_RX_RESET:
		msg = (struct msgb*)data;
		lb_peer_rx_reset(lbp, msg);
		return;

	default:
		LOG_LB_PEER(lbp, LOGL_ERROR, "Unhandled event: %s\n", osmo_fsm_event_name(&lb_peer_fsm, event));
		return;
	}
}

void lb_peer_st_wait_rx_reset_ack(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct lb_peer *lbp = fi->priv;
	struct lb_peer_ev_ctx *ctx;
	struct msgb *msg;

	switch (event) {

	case LB_PEER_EV_RX_RESET_ACK:
		msg = (struct msgb*)data;
		lb_peer_rx_reset_ack(lbp, msg);
		return;

	case LB_PEER_EV_MSG_UP_CO:
	case LB_PEER_EV_MSG_UP_CO_INITIAL:
		ctx = data;
		OSMO_ASSERT(ctx);
		LOG_LB_PEER(lbp, LOGL_ERROR, "Receiving CO message on Lb peer that has not done a proper RESET yet."
			     " Disconnecting on incoming message, sending RESET to Lb peer.\n");
		sccp_lb_disconnect(lbp->sli, ctx->conn_id, 0);
		/* No valid RESET procedure has happened here yet. */
		lb_peer_reset(lbp);
		return;

	case LB_PEER_EV_RX_RESET:
		msg = (struct msgb*)data;
		lb_peer_rx_reset(lbp, msg);
		return;

	default:
		LOG_LB_PEER(lbp, LOGL_ERROR, "Unhandled event: %s\n", osmo_fsm_event_name(&lb_peer_fsm, event));
		return;
	}
}

void lb_peer_st_ready(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct lb_peer *lbp = fi->priv;
	struct lb_peer_ev_ctx *ctx;
	struct lb_conn *lb_conn;
	struct msgb *msg;

	switch (event) {

	case LB_PEER_EV_MSG_UP_CO_INITIAL:
		ctx = data;
		OSMO_ASSERT(ctx);
		OSMO_ASSERT(!ctx->lb_conn);
		OSMO_ASSERT(ctx->msg);

		lb_conn = lb_conn_create_incoming(lbp, ctx->conn_id, __func__);
		if (!lb_conn) {
			LOG_LB_PEER(lbp, LOGL_ERROR, "Cannot allocate lb_conn\n");
			return;
		}

		lb_conn_rx(lb_conn, ctx->msg, true);
		lb_conn_put(lb_conn, __func__);
		return;

	case LB_PEER_EV_MSG_UP_CO:
		ctx = data;
		OSMO_ASSERT(ctx);
		OSMO_ASSERT(ctx->lb_conn);
		OSMO_ASSERT(ctx->msg);

		lb_conn_rx(ctx->lb_conn, ctx->msg, false);
		return;

	case LB_PEER_EV_MSG_DOWN_CO_INITIAL:
		ctx = data;
		OSMO_ASSERT(ctx);
		OSMO_ASSERT(ctx->msg);
		sccp_lb_down_l2_co_initial(lbp->sli, &lbp->peer_addr, ctx->conn_id, ctx->msg);
		return;

	case LB_PEER_EV_MSG_DOWN_CO:
		ctx = data;
		OSMO_ASSERT(ctx);
		OSMO_ASSERT(ctx->msg);
		sccp_lb_down_l2_co(lbp->sli, ctx->conn_id, ctx->msg);
		return;

	case LB_PEER_EV_MSG_DOWN_CL:
		OSMO_ASSERT(data);
		sccp_lb_down_l2_cl(lbp->sli, &lbp->peer_addr, (struct msgb*)data);
		return;

	case LB_PEER_EV_RX_RESET:
		msg = (struct msgb*)data;
		lb_peer_rx_reset(lbp, msg);
		return;

	default:
		LOG_LB_PEER(lbp, LOGL_ERROR, "Unhandled event: %s\n", osmo_fsm_event_name(&lb_peer_fsm, event));
		return;
	}
}

static int lb_peer_fsm_timer_cb(struct osmo_fsm_inst *fi)
{
	struct lb_peer *lbp = fi->priv;
	lb_peer_state_chg(lbp, LB_PEER_ST_WAIT_RX_RESET);
	return 0;
}

void lb_peer_fsm_cleanup(struct osmo_fsm_inst *fi, enum osmo_fsm_term_cause cause)
{
	struct lb_peer *lbp = fi->priv;
	lb_peer_discard_all_conns(lbp);
	llist_del(&lbp->entry);
}

static const struct value_string lb_peer_fsm_event_names[] = {
	OSMO_VALUE_STRING(LB_PEER_EV_MSG_UP_CL),
	OSMO_VALUE_STRING(LB_PEER_EV_MSG_UP_CO_INITIAL),
	OSMO_VALUE_STRING(LB_PEER_EV_MSG_UP_CO),
	OSMO_VALUE_STRING(LB_PEER_EV_MSG_DOWN_CL),
	OSMO_VALUE_STRING(LB_PEER_EV_MSG_DOWN_CO_INITIAL),
	OSMO_VALUE_STRING(LB_PEER_EV_MSG_DOWN_CO),
	OSMO_VALUE_STRING(LB_PEER_EV_RX_RESET),
	OSMO_VALUE_STRING(LB_PEER_EV_RX_RESET_ACK),
	OSMO_VALUE_STRING(LB_PEER_EV_CONNECTION_SUCCESS),
	OSMO_VALUE_STRING(LB_PEER_EV_CONNECTION_TIMEOUT),
	{}
};

#define S(x)	(1 << (x))

static const struct osmo_fsm_state lb_peer_fsm_states[] = {
	[LB_PEER_ST_WAIT_RX_RESET] = {
		.name = "WAIT_RX_RESET",
		.action = lb_peer_st_wait_rx_reset,
		.in_event_mask = 0
			| S(LB_PEER_EV_RX_RESET)
			| S(LB_PEER_EV_MSG_UP_CO_INITIAL)
			| S(LB_PEER_EV_MSG_UP_CO)
			| S(LB_PEER_EV_CONNECTION_TIMEOUT)
			,
		.out_state_mask = 0
			| S(LB_PEER_ST_WAIT_RX_RESET)
			| S(LB_PEER_ST_WAIT_RX_RESET_ACK)
			| S(LB_PEER_ST_READY)
			| S(LB_PEER_ST_DISCARDING)
			,
	},
	[LB_PEER_ST_WAIT_RX_RESET_ACK] = {
		.name = "WAIT_RX_RESET_ACK",
		.action = lb_peer_st_wait_rx_reset_ack,
		.in_event_mask = 0
			| S(LB_PEER_EV_RX_RESET)
			| S(LB_PEER_EV_RX_RESET_ACK)
			| S(LB_PEER_EV_MSG_UP_CO_INITIAL)
			| S(LB_PEER_EV_MSG_UP_CO)
			| S(LB_PEER_EV_CONNECTION_TIMEOUT)
			,
		.out_state_mask = 0
			| S(LB_PEER_ST_WAIT_RX_RESET)
			| S(LB_PEER_ST_WAIT_RX_RESET_ACK)
			| S(LB_PEER_ST_READY)
			| S(LB_PEER_ST_DISCARDING)
			,
	},
	[LB_PEER_ST_READY] = {
		.name = "READY",
		.action = lb_peer_st_ready,
		.in_event_mask = 0
			| S(LB_PEER_EV_RX_RESET)
			| S(LB_PEER_EV_MSG_UP_CO_INITIAL)
			| S(LB_PEER_EV_MSG_UP_CO)
			| S(LB_PEER_EV_MSG_DOWN_CO_INITIAL)
			| S(LB_PEER_EV_MSG_DOWN_CO)
			| S(LB_PEER_EV_MSG_DOWN_CL)
			,
		.out_state_mask = 0
			| S(LB_PEER_ST_WAIT_RX_RESET)
			| S(LB_PEER_ST_WAIT_RX_RESET_ACK)
			| S(LB_PEER_ST_READY)
			| S(LB_PEER_ST_DISCARDING)
			,
	},
	[LB_PEER_ST_DISCARDING] = {
		.name = "DISCARDING",
	},
};

static struct osmo_fsm lb_peer_fsm = {
	.name = "lb_peer",
	.states = lb_peer_fsm_states,
	.num_states = ARRAY_SIZE(lb_peer_fsm_states),
	.log_subsys = DLB,
	.event_names = lb_peer_fsm_event_names,
	.timer_cb = lb_peer_fsm_timer_cb,
	.cleanup = lb_peer_fsm_cleanup,
	.allstate_action = lb_peer_allstate_action,
	.allstate_event_mask = 0
		| S(LB_PEER_EV_MSG_UP_CL)
		,
};

int lb_peer_up_l2(struct sccp_lb_inst *sli, const struct osmo_sccp_addr *calling_addr, bool co, uint32_t conn_id,
		  struct msgb *l2)
{
	struct lb_peer *lb_peer = NULL;
	uint32_t event;
	struct lb_peer_ev_ctx ctx = {
		.conn_id = conn_id,
		.msg = l2,
	};

	if (co) {
		struct lb_conn *lb_conn;
		llist_for_each_entry(lb_conn, &sli->lb_conns, entry) {
			if (lb_conn->sccp_conn_id == conn_id) {
				lb_peer = lb_conn->lb_peer;
				ctx.lb_conn = lb_conn;
				break;
			}
		}

		if (lb_peer && calling_addr) {
			LOG_SCCP_LB_CO(sli, calling_addr, conn_id, LOGL_ERROR,
					"Connection-Oriented Initial message for already existing conn_id."
					" Dropping message.\n");
			return -EINVAL;
		}

		if (!lb_peer && !calling_addr) {
			LOG_SCCP_LB_CO(sli, calling_addr, conn_id, LOGL_ERROR,
					"Connection-Oriented non-Initial message for unknown conn_id %u."
					" Dropping message.\n", conn_id);
			return -EINVAL;
		}
	}

	if (calling_addr) {
		lb_peer = lb_peer_find_or_create(sli, calling_addr);
		if (!lb_peer) {
			LOG_SCCP_LB_CL(sli, calling_addr, LOGL_ERROR, "Cannot register Lb peer\n");
			return -EIO;
		}
	}

	OSMO_ASSERT(lb_peer && lb_peer->fi);

	if (co)
		event = calling_addr ? LB_PEER_EV_MSG_UP_CO_INITIAL : LB_PEER_EV_MSG_UP_CO;
	else
		event = LB_PEER_EV_MSG_UP_CL;

	return osmo_fsm_inst_dispatch(lb_peer->fi, event, &ctx);
}

void lb_peer_disconnect(struct sccp_lb_inst *sli, uint32_t conn_id)
{
	struct lb_conn *lb_conn;
	llist_for_each_entry(lb_conn, &sli->lb_conns, entry) {
		if (lb_conn->sccp_conn_id == conn_id) {
			lb_conn_discard(lb_conn);
			return;
		}
	}
}
