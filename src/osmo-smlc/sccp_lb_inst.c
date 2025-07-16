/*
 * (C) 2020-2025 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
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

#include <osmocom/core/logging.h>

#include <osmocom/sccp/sccp_types.h>
#include <osmocom/sigtran/sccp_sap.h>
#include <osmocom/sigtran/sccp_helpers.h>

#include <osmocom/smlc/debug.h>
#include <osmocom/smlc/smlc_data.h>
#include <osmocom/smlc/sccp_lb_inst.h>
#include <osmocom/smlc/lb_peer.h>

/* We need an unused SCCP conn_id across all SCCP users. */
int sccp_lb_inst_next_conn_id()
{
	static uint32_t next_id = 1;
	int i;

	/* This looks really suboptimal, but in most cases the static next_id should indicate exactly the next unused
	 * conn_id, and we only iterate all conns once to make super sure that it is not already in use. */

	for (i = 0; i < 0xFFFFFF; i++) {
		struct lb_peer *lb_peer;
		uint32_t conn_id = next_id;
		bool conn_id_already_used = false;
		next_id = (next_id + 1) & 0xffffff;

		llist_for_each_entry(lb_peer, &g_smlc->lb->lb_peers, entry) {
			struct lb_conn *conn;
			lb_peer_for_each_lb_conn(conn, lb_peer) {
				if (conn_id == conn->sccp_conn_id) {
					conn_id_already_used = true;
					break;
				}
			}
			if (conn_id_already_used)
				break;
		}

		if (!conn_id_already_used)
			return conn_id;
	}
	return -1;
}

static int sccp_lb_sap_up(struct osmo_prim_hdr *oph, void *_scu);

struct sccp_lb_inst *sccp_lb_init(void *talloc_ctx, struct osmo_sccp_instance *sccp, enum osmo_sccp_ssn ssn,
				  const char *sccp_user_name)
{
	struct sccp_lb_inst *sli = talloc(talloc_ctx, struct sccp_lb_inst);
	OSMO_ASSERT(sli);
	*sli = (struct sccp_lb_inst){
		.sccp = sccp,
	};

	INIT_LLIST_HEAD(&sli->lb_peers);
	INIT_LLIST_HEAD(&sli->lb_conns);

	osmo_sccp_local_addr_by_instance(&sli->local_sccp_addr, sccp, ssn);
	sli->scu = osmo_sccp_user_bind(sccp, sccp_user_name, sccp_lb_sap_up, ssn);
	osmo_sccp_user_set_priv(sli->scu, sli);

	return sli;
}

static void handle_notice_ind(struct sccp_lb_inst *sli, const struct osmo_scu_notice_param *ni)
{
	struct lb_peer *lbp;

	lbp = lb_peer_find(sli, &ni->calling_addr);
	if (!lbp) {
		LOG_SCCP_LB(sli, LOGL_DEBUG, "(calling_addr=%s) N-NOTICE.ind cause=%u='%s' importance=%u didn't match any lb_peer, ignoring\n",
			    osmo_sccp_addr_dump(&ni->calling_addr),
			    ni->cause, osmo_sccp_return_cause_name(ni->cause),
			    ni->importance);
		return;
	}

	LOG_LB_PEER(lbp, LOGL_NOTICE, "N-NOTICE.ind cause=%u='%s' importance=%u\n",
		     ni->cause, osmo_sccp_return_cause_name(ni->cause),
		     ni->importance);

	switch (ni->cause) {
	case SCCP_RETURN_CAUSE_SUBSYSTEM_CONGESTION:
	case SCCP_RETURN_CAUSE_NETWORK_CONGESTION:
		/* Transient failures (hopefully), keep going. */
		return;
	default:
		break;
	}

	/* Messages are not arriving to lb_peer. Signal it is unavailable to update local state. */
	osmo_fsm_inst_dispatch(lbp->fi, LB_PEER_EV_UNAVAILABLE, NULL);
}

static void handle_pcstate_ind(struct sccp_lb_inst *sli, const struct osmo_scu_pcstate_param *pcst)
{
	struct osmo_ss7_instance *cs7 = osmo_sccp_get_ss7(sli->sccp);
	struct lb_peer *lbp;
	bool connected;
	bool disconnected;

	LOG_SCCP_LB(sli, LOGL_DEBUG, "N-PCSTATE ind: affected_pc=%u=%s sp_status=%s remote_sccp_status=%s\n",
		    pcst->affected_pc, osmo_ss7_pointcode_print(cs7, pcst->affected_pc),
		    osmo_sccp_sp_status_name(pcst->sp_status),
		    osmo_sccp_rem_sccp_status_name(pcst->remote_sccp_status));

	/* If we don't care about that point-code, ignore PCSTATE. */
	lbp = lb_peer_find_by_pc(sli, pcst->affected_pc);
	if (!lbp) {
		LOG_SCCP_LB(sli, LOGL_DEBUG, "No lb_peer found under pc=%u=%s\n",
			    pcst->affected_pc, osmo_ss7_pointcode_print(cs7, pcst->affected_pc));
		return;
	}

	/* See if this marks the point code to have become available, or to have been lost.
	 *
	 * I want to detect two events:
	 * - connection event (both indicators say PC is reachable).
	 * - disconnection event (at least one indicator says the PC is not reachable).
	 *
	 * There are two separate incoming indicators with various possible values -- the incoming events can be:
	 *
	 * - neither connection nor disconnection indicated -- just indicating congestion
	 *   connected == false, disconnected == false --> do nothing.
	 * - both incoming values indicate that we are connected
	 *   --> trigger connected
	 * - both indicate we are disconnected
	 *   --> trigger disconnected
	 * - one value indicates 'connected', the other indicates 'disconnected'
	 *   --> trigger disconnected
	 *
	 * Congestion could imply that we're connected, but it does not indicate that a PC's reachability changed, so no need to
	 * trigger on that.
	 */
	connected = false;
	disconnected = false;

	switch (pcst->sp_status) {
	case OSMO_SCCP_SP_S_ACCESSIBLE:
		connected = true;
		break;
	case OSMO_SCCP_SP_S_INACCESSIBLE:
		disconnected = true;
		break;
	default:
	case OSMO_SCCP_SP_S_CONGESTED:
		/* Neither connecting nor disconnecting */
		break;
	}

	switch (pcst->remote_sccp_status) {
	case OSMO_SCCP_REM_SCCP_S_AVAILABLE:
		if (!disconnected)
			connected = true;
		break;
	case OSMO_SCCP_REM_SCCP_S_UNAVAILABLE_UNKNOWN:
	case OSMO_SCCP_REM_SCCP_S_UNEQUIPPED:
	case OSMO_SCCP_REM_SCCP_S_INACCESSIBLE:
		disconnected = true;
		connected = false;
		break;
	default:
	case OSMO_SCCP_REM_SCCP_S_CONGESTED:
		/* Neither connecting nor disconnecting */
		break;
	}

	if (disconnected) {
		LOG_SCCP_LB(sli, LOGL_NOTICE,
			    "now unreachable: N-PCSTATE ind: pc=%u=%s sp_status=%s remote_sccp_status=%s\n",
			    pcst->affected_pc, osmo_ss7_pointcode_print(cs7, pcst->affected_pc),
			    osmo_sccp_sp_status_name(pcst->sp_status),
			    osmo_sccp_rem_sccp_status_name(pcst->remote_sccp_status));
		osmo_fsm_inst_dispatch(lbp->fi, LB_PEER_EV_UNAVAILABLE, NULL);
	} else if (connected) {
		LOG_SCCP_LB(sli, LOGL_NOTICE,
			    "now available: N-PCSTATE ind: pc=%u=%s sp_status=%s remote_sccp_status=%s\n",
			    pcst->affected_pc, osmo_ss7_pointcode_print(cs7, pcst->affected_pc),
			    osmo_sccp_sp_status_name(pcst->sp_status),
			    osmo_sccp_rem_sccp_status_name(pcst->remote_sccp_status));
		osmo_fsm_inst_dispatch(lbp->fi, LB_PEER_EV_AVAILABLE, NULL);
	}
}

static int sccp_lb_sap_up(struct osmo_prim_hdr *oph, void *_scu)
{
	struct osmo_sccp_user *scu = _scu;
	struct sccp_lb_inst *sli = osmo_sccp_user_get_priv(scu);
	struct osmo_scu_prim *prim = (struct osmo_scu_prim *) oph;
	struct osmo_sccp_addr *my_addr;
	struct osmo_sccp_addr *peer_addr;
	uint32_t conn_id;
	int rc;

	switch (OSMO_PRIM_HDR(oph)) {
	case OSMO_PRIM(OSMO_SCU_PRIM_N_CONNECT, PRIM_OP_INDICATION):
		/* indication of new inbound connection request */
		conn_id = prim->u.connect.conn_id;
		my_addr = &prim->u.connect.called_addr;
		peer_addr = &prim->u.connect.calling_addr;
		LOG_SCCP_LB_CO(sli, peer_addr, conn_id, LOGL_DEBUG, "%s(%s)\n", __func__, osmo_scu_prim_name(oph));

		if (!msgb_l2(oph->msg) || msgb_l2len(oph->msg) == 0) {
			LOG_SCCP_LB_CO(sli, peer_addr, conn_id, LOGL_NOTICE, "Received invalid N-CONNECT.ind\n");
			rc = -1;
			break;
		}

		if (osmo_sccp_addr_ri_cmp(&sli->local_sccp_addr, my_addr))
			LOG_SCCP_LB_CO(sli, peer_addr, conn_id, LOGL_ERROR,
				       "Rx N-CONNECT: Called address is %s != local address %s\n",
				       osmo_sccp_inst_addr_to_str_c(OTC_SELECT, sli->sccp, my_addr),
				       osmo_sccp_inst_addr_to_str_c(OTC_SELECT, sli->sccp, &sli->local_sccp_addr));

		/* ensure the local SCCP socket is ACTIVE */
		osmo_sccp_tx_conn_resp(scu, conn_id, my_addr, NULL, 0);

		rc = lb_peer_up_l2(sli, peer_addr, true, conn_id, oph->msg);
		if (rc)
			osmo_sccp_tx_disconn(scu, conn_id, my_addr, SCCP_RETURN_CAUSE_UNQUALIFIED);
		break;

	case OSMO_PRIM(OSMO_SCU_PRIM_N_DATA, PRIM_OP_INDICATION):
		/* connection-oriented data received */
		conn_id = prim->u.data.conn_id;
		LOG_SCCP_LB_CO(sli, NULL, conn_id, LOGL_DEBUG, "%s(%s)\n", __func__, osmo_scu_prim_name(oph));

		rc = lb_peer_up_l2(sli, NULL, true, conn_id, oph->msg);
		break;

	case OSMO_PRIM(OSMO_SCU_PRIM_N_DISCONNECT, PRIM_OP_INDICATION):
		/* indication of disconnect */
		conn_id = prim->u.disconnect.conn_id;
		LOG_SCCP_LB_CO(sli, NULL, conn_id, LOGL_DEBUG, "%s(%s)\n", __func__, osmo_scu_prim_name(oph));

		/* If there is no L2 payload in the N-DISCONNECT, no need to dispatch up_l2(). */
		if (msgb_l2len(oph->msg))
			rc = lb_peer_up_l2(sli, NULL, true, conn_id, oph->msg);
		else
			rc = 0;

		/* Make sure the lb_conn is dropped. It might seem more optimal to combine the disconnect() into
		 * up_l2(), but since an up_l2() dispatch might already cause the lb_conn to be discarded for other
		 * reasons, a separate disconnect() with a separate conn_id lookup is actually necessary. */
		sccp_lb_disconnect(sli, conn_id, 0);
		break;

	case OSMO_PRIM(OSMO_SCU_PRIM_N_UNITDATA, PRIM_OP_INDICATION):
		/* connection-less data received */
		my_addr = &prim->u.unitdata.called_addr;
		peer_addr = &prim->u.unitdata.calling_addr;
		LOG_SCCP_LB_CL(sli, peer_addr, LOGL_DEBUG, "%s(%s)\n", __func__, osmo_scu_prim_name(oph));

		if (osmo_sccp_addr_ri_cmp(&sli->local_sccp_addr, my_addr))
			LOG_SCCP_LB_CL(sli, peer_addr, LOGL_ERROR,
					"Rx N-UNITDATA: Called address is %s != local address %s\n",
					osmo_sccp_inst_addr_to_str_c(OTC_SELECT, sli->sccp, my_addr),
					osmo_sccp_inst_addr_to_str_c(OTC_SELECT, sli->sccp, &sli->local_sccp_addr));

		rc = lb_peer_up_l2(sli, peer_addr, false, 0, oph->msg);
		break;

	case OSMO_PRIM(OSMO_SCU_PRIM_N_NOTICE, PRIM_OP_INDICATION):
		handle_notice_ind(sli, &prim->u.notice);
		rc = 0;
		break;

	case OSMO_PRIM(OSMO_SCU_PRIM_N_PCSTATE, PRIM_OP_INDICATION):
		handle_pcstate_ind(sli, &prim->u.pcstate);
		rc = 0;
		break;

	case OSMO_PRIM(OSMO_SCU_PRIM_N_STATE, PRIM_OP_INDICATION):
		LOG_SCCP_LB(sli, LOGL_INFO,
			    "SCCP-User-SAP: Ignoring %s.%s\n",
			    osmo_scu_prim_type_name(oph->primitive),
			    get_value_string(osmo_prim_op_names, oph->operation));
		rc = 0;
		break;

	default:
		LOG_SCCP_LB_CL(sli, NULL, LOGL_ERROR, "%s(%s) unsupported\n", __func__, osmo_scu_prim_name(oph));
		rc = -1;
		break;
	}

	msgb_free(oph->msg);
	return rc;
}

/* Push some padding if necessary to reach a multiple-of-eight offset to be msgb_push() an osmo_scu_prim that will then
 * be 8-byte aligned. */
static void msgb_pad_mod8(struct msgb *msg)
{
	uint8_t mod8 = (intptr_t)(msg->data) % 8;
	if (mod8)
		msgb_push(msg, mod8);
}

static int sccp_lb_sap_down(struct sccp_lb_inst *sli, struct osmo_prim_hdr *oph)
{
	int rc;
	if (!sli->scu) {
		rate_ctr_inc(rate_ctr_group_get_ctr(g_smlc->ctrs, SMLC_CTR_BSSMAP_LE_TX_ERR_CONN_NOT_READY));
		return -EIO;
	}
	rc = osmo_sccp_user_sap_down_nofree(sli->scu, oph);
	if (rc >= 0)
		rate_ctr_inc(rate_ctr_group_get_ctr(g_smlc->ctrs, SMLC_CTR_BSSMAP_LE_TX_SUCCESS));
	else
		rate_ctr_inc(rate_ctr_group_get_ctr(g_smlc->ctrs, SMLC_CTR_BSSMAP_LE_TX_ERR_SEND));
	return rc;
}

int sccp_lb_down_l2_co_initial(struct sccp_lb_inst *sli,
				const struct osmo_sccp_addr *called_addr,
				uint32_t conn_id, struct msgb *l2)
{
	struct osmo_scu_prim *prim;

	l2->l2h = l2->data;

	msgb_pad_mod8(l2);
	prim = (struct osmo_scu_prim *) msgb_push(l2, sizeof(*prim));
	prim->u.connect = (struct osmo_scu_connect_param){
		.called_addr = *called_addr,
		.calling_addr = sli->local_sccp_addr,
		.sccp_class = 2,
		//.importance = ?,
		.conn_id = conn_id,
	};
	osmo_prim_init(&prim->oph, SCCP_SAP_USER, OSMO_SCU_PRIM_N_CONNECT, PRIM_OP_REQUEST, l2);
	return sccp_lb_sap_down(sli, &prim->oph);
}

int sccp_lb_down_l2_co(struct sccp_lb_inst *sli, uint32_t conn_id, struct msgb *l2)
{
	struct osmo_scu_prim *prim;

	l2->l2h = l2->data;

	msgb_pad_mod8(l2);
	prim = (struct osmo_scu_prim *) msgb_push(l2, sizeof(*prim));
	prim->u.data.conn_id = conn_id;
	osmo_prim_init(&prim->oph, SCCP_SAP_USER, OSMO_SCU_PRIM_N_DATA, PRIM_OP_REQUEST, l2);
	return sccp_lb_sap_down(sli, &prim->oph);
}

int sccp_lb_down_l2_cl(struct sccp_lb_inst *sli, const struct osmo_sccp_addr *called_addr, struct msgb *l2)
{
	struct osmo_scu_prim *prim;

	l2->l2h = l2->data;

	msgb_pad_mod8(l2);
	prim = (struct osmo_scu_prim *) msgb_push(l2, sizeof(*prim));
	prim->u.unitdata = (struct osmo_scu_unitdata_param){
		.called_addr = *called_addr,
		.calling_addr = sli->local_sccp_addr,
	};
	osmo_prim_init(&prim->oph, SCCP_SAP_USER, OSMO_SCU_PRIM_N_UNITDATA, PRIM_OP_REQUEST, l2);
	return sccp_lb_sap_down(sli, &prim->oph);
}

int sccp_lb_disconnect(struct sccp_lb_inst *sli, uint32_t conn_id, uint32_t cause)
{
	return osmo_sccp_tx_disconn(sli->scu, conn_id, NULL, cause);
}
