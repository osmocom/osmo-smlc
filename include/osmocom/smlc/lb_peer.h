#pragma once

#include <osmocom/core/linuxlist.h>
#include <osmocom/gsm/gsm0808.h>
#include <osmocom/sigtran/sccp_sap.h>

#include <osmocom/smlc/debug.h>
#include <osmocom/smlc/lb_conn.h>

struct vlr_subscr;
struct lb_conn;
struct neighbor_ident_entry;

#define LOG_LB_PEER_CAT(LB_PEER, subsys, loglevel, fmt, args ...) \
	LOGPFSMSL((LB_PEER)? (LB_PEER)->fi : NULL, subsys, loglevel, fmt, ## args)

#define LOG_LB_PEER(LB_PEER, loglevel, fmt, args ...) \
	LOG_LB_PEER_CAT(LB_PEER, DLB, loglevel, fmt, ## args)

struct lb_peer {
	struct llist_head entry;
	struct osmo_fsm_inst *fi;

	struct sccp_lb_inst *sli;
	struct osmo_sccp_addr peer_addr;
};

#define lb_peer_for_each_lb_conn(LB_CONN, LB_PEER) \
	llist_for_each_entry(LB_CONN, &(LB_PEER)->sli->lb_conns, entry) \
		if ((LB_CONN)->lb_peer == (LB_PEER))

#define lb_peer_for_each_lb_conn_safe(LB_CONN, LB_CONN_NEXT, LB_PEER) \
	llist_for_each_entry_safe(LB_CONN, LB_CONN_NEXT, &(LB_PEER)->sli->lb_conns, entry) \
		if ((LB_CONN)->lb_peer == (LB_PEER))

enum lb_peer_state {
	LB_PEER_ST_WAIT_RX_RESET = 0,
	LB_PEER_ST_WAIT_RX_RESET_ACK,
	LB_PEER_ST_READY,
	LB_PEER_ST_DISCARDING,
};

enum lb_peer_event {
	LB_PEER_EV_MSG_UP_CL = 0,
	LB_PEER_EV_MSG_UP_CO_INITIAL,
	LB_PEER_EV_MSG_UP_CO,
	LB_PEER_EV_MSG_DOWN_CL,
	LB_PEER_EV_MSG_DOWN_CO_INITIAL,
	LB_PEER_EV_MSG_DOWN_CO,
	LB_PEER_EV_RX_RESET,
	LB_PEER_EV_RX_RESET_ACK,
};

struct lb_peer_ev_ctx {
	uint32_t conn_id;
	struct lb_conn *lb_conn;
	struct msgb *msg;
};

struct lb_peer *lb_peer_find_or_create(struct sccp_lb_inst *sli, const struct osmo_sccp_addr *peer_addr);
struct lb_peer *lb_peer_find(const struct sccp_lb_inst *sli, const struct osmo_sccp_addr *peer_addr);

int lb_peer_up_l2(struct sccp_lb_inst *sli, const struct osmo_sccp_addr *calling_addr, bool co, uint32_t conn_id,
		  struct msgb *l2);
