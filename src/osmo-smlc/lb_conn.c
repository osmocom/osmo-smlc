/* SMLC Lb connection implementation */

/*
 * (C) 2020 by sysmocom s.m.f.c. <info@sysmocom.de>
 * All Rights Reserved
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
 *
 */

#include <errno.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/fsm.h>
#include <osmocom/core/signal.h>
#include <osmocom/gsm/bssmap_le.h>

#include <osmocom/smlc/debug.h>
#include <osmocom/smlc/smlc_data.h>
#include <osmocom/smlc/sccp_lb_inst.h>
#include <osmocom/smlc/lb_peer.h>
#include <osmocom/smlc/lb_conn.h>
#include <osmocom/smlc/smlc_loc_req.h>

static int lb_conn_use_cb(struct osmo_use_count_entry *e, int32_t old_use_count, const char *file, int line)
{
	struct lb_conn *lb_conn = e->use_count->talloc_object;
	int32_t total;
	int level;

	if (!e->use)
		return -EINVAL;

	total = osmo_use_count_total(&lb_conn->use_count);

	if (total == 0
	    || (total == 1 && old_use_count == 0 && e->count == 1))
		level = LOGL_INFO;
	else
		level = LOGL_DEBUG;

	LOG_LB_CONN_SL(lb_conn, DREF, level, file, line, "%s %s: now used by %s\n",
		(e->count - old_use_count) > 0? "+" : "-", e->use,
		osmo_use_count_to_str_c(OTC_SELECT, &lb_conn->use_count));

	if (e->count < 0)
		return -ERANGE;

	if (total == 0)
		lb_conn_close(lb_conn);
	return 0;
}

static struct lb_conn *lb_conn_alloc(struct lb_peer *lb_peer, uint32_t sccp_conn_id, const char *use_token)
{
	struct lb_conn *lb_conn;

	lb_conn = talloc(lb_peer, struct lb_conn);
	OSMO_ASSERT(lb_conn);

	*lb_conn = (struct lb_conn){
		.lb_peer = lb_peer,
		.sccp_conn_id = sccp_conn_id,
		.use_count = {
			.talloc_object = lb_conn,
			.use_cb = lb_conn_use_cb,
		},
	};

	llist_add(&lb_conn->entry, &lb_peer->sli->lb_conns);
	lb_conn_get(lb_conn, use_token);
	return lb_conn;
}

struct lb_conn *lb_conn_create_incoming(struct lb_peer *lb_peer, uint32_t sccp_conn_id, const char *use_token)
{
	LOG_LB_PEER(lb_peer, LOGL_DEBUG, "Incoming lb_conn id: %u\n", sccp_conn_id);
	return lb_conn_alloc(lb_peer, sccp_conn_id, use_token);
}

struct lb_conn *lb_conn_create_outgoing(struct lb_peer *lb_peer, const char *use_token)
{
	int new_conn_id = sccp_lb_inst_next_conn_id();
	if (new_conn_id < 0)
		return NULL;
	LOG_LB_PEER(lb_peer, LOGL_DEBUG, "Outgoing lb_conn id: %u\n", new_conn_id);
	return lb_conn_alloc(lb_peer, new_conn_id, use_token);
}

struct lb_conn *lb_conn_find_by_smlc_subscr(struct smlc_subscr *smlc_subscr, const char *use_token)
{
	struct lb_conn *lb_conn;
	llist_for_each_entry(lb_conn, &g_smlc->lb->lb_conns, entry) {
		if (lb_conn->smlc_subscr == smlc_subscr) {
			lb_conn_get(lb_conn, use_token);
			return lb_conn;
		}
	}
	return NULL;
}

int lb_conn_down_l2_co(struct lb_conn *lb_conn, struct msgb *l3, bool initial)
{
	struct lb_peer_ev_ctx co = {
		.conn_id = lb_conn->sccp_conn_id,
		.lb_conn = lb_conn,
		.msg = l3,
	};
	if (!lb_conn->lb_peer)
		return -EIO;
	return osmo_fsm_inst_dispatch(lb_conn->lb_peer->fi,
				      initial ? LB_PEER_EV_MSG_DOWN_CO_INITIAL : LB_PEER_EV_MSG_DOWN_CO,
				      &co);
}

int lb_conn_rx(struct lb_conn *lb_conn, struct msgb *msg, bool initial)
{
	struct bssap_le_pdu bssap_le;
	struct osmo_bssap_le_err *err;
	if (osmo_bssap_le_dec(&bssap_le, &err, msg, msg)) {
		LOG_LB_CONN(lb_conn, LOGL_ERROR, "Rx BSSAP-LE with error: %s\n", err->logmsg);
		return -EINVAL;
	}

	return smlc_loc_req_rx_bssap_le(lb_conn, &bssap_le);
}

int lb_conn_send_bssmap_le(struct lb_conn *lb_conn, const struct bssmap_le_pdu *bssmap_le)
{
	struct msgb *msg;
	int rc;
	struct bssap_le_pdu bssap_le = {
		.discr = BSSAP_LE_MSG_DISCR_BSSMAP_LE,
		.bssmap_le = *bssmap_le,
	};

	msg = osmo_bssap_le_enc(&bssap_le);
	if (!msg) {
		LOG_LB_CONN(lb_conn, LOGL_ERROR, "Unable to encode %s\n",
			    osmo_bssap_le_pdu_to_str_c(OTC_SELECT, &bssap_le));
		return -EINVAL;
	}
	rc = lb_conn_down_l2_co(lb_conn, msg, false);
	msgb_free(msg);
	if (rc)
		LOG_LB_CONN(lb_conn, LOGL_ERROR, "Unable to send %s\n",
			    osmo_bssap_le_pdu_to_str_c(OTC_SELECT, &bssap_le));
	return rc;
}

/* Regularly close the lb_conn */
void lb_conn_close(struct lb_conn *lb_conn)
{
	if (!lb_conn)
		return;
	if (lb_conn->closing)
		return;
	lb_conn->closing = true;
	LOG_LB_PEER(lb_conn->lb_peer, LOGL_DEBUG, "Closing lb_conn\n");

	if (lb_conn->lb_peer) {
		/* Todo: pass a useful SCCP cause? */
		sccp_lb_disconnect(lb_conn->lb_peer->sli, lb_conn->sccp_conn_id, 0);
		lb_conn->lb_peer = NULL;
	}

	if (lb_conn->smlc_loc_req)
		osmo_fsm_inst_term(lb_conn->smlc_loc_req->fi, OSMO_FSM_TERM_REGULAR, NULL);

	if (lb_conn->smlc_subscr)
		smlc_subscr_put(lb_conn->smlc_subscr, SMLC_SUBSCR_USE_LB_CONN);

	llist_del(&lb_conn->entry);
	talloc_free(lb_conn);
}

/* Same as lb_conn_close() but without sending any SCCP messages (e.g. after RESET) */
void lb_conn_discard(struct lb_conn *lb_conn)
{
	if (!lb_conn)
		return;
	/* Make sure to drop dead and don't dispatch things like DISCONNECT requests on SCCP. */
	lb_conn->lb_peer = NULL;
	lb_conn_close(lb_conn);
}
