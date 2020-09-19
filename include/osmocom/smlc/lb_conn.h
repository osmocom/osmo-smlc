#pragma once
/* SMLC Lb connection implementation */

#include <stdint.h>

#include <osmocom/core/linuxlist.h>
#include <osmocom/smlc/smlc_subscr.h>

struct lb_peer;
struct osmo_fsm_inst;
struct msgb;
struct bssmap_le_pdu;

#define LOG_LB_CONN_SL(CONN, CAT, LEVEL, file, line, FMT, args...) \
	LOGPSRC(CAT, LEVEL, file, line, "Lb-%d %s %s: " FMT, (CONN) ? (CONN)->sccp_conn_id : 0, \
	     ((CONN) && (CONN)->smlc_subscr) ? smlc_subscr_to_str_c(OTC_SELECT, (CONN)->smlc_subscr) : "no-subscr", \
	     (CONN) ? osmo_use_count_to_str_c(OTC_SELECT, &(CONN)->use_count) : "-", \
	     ##args)

#define LOG_LB_CONN_S(CONN, CAT, LEVEL, FMT, args...) \
	LOG_LB_CONN_SL(CONN, CAT, LEVEL, NULL, 0, FMT, ##args)

#define LOG_LB_CONN(CONN, LEVEL, FMT, args...) \
	LOG_LB_CONN_S(CONN, DLB, LEVEL, FMT, ##args)

#define SMLC_SUBSCR_USE_LB_CONN "Lb-conn"

struct lb_conn {
	struct llist_head entry;
	struct osmo_use_count use_count;

	struct lb_peer *lb_peer;
	uint32_t sccp_conn_id;

	bool closing;

	struct smlc_subscr *smlc_subscr;
	struct smlc_loc_req *smlc_loc_req;
};

#define lb_conn_get(lb_conn, use) \
	OSMO_ASSERT(osmo_use_count_get_put(&(lb_conn)->use_count, use, 1) == 0)
#define lb_conn_put(lb_conn, use) \
	OSMO_ASSERT(osmo_use_count_get_put(&(lb_conn)->use_count, use, -1) == 0)

struct lb_conn *lb_conn_create_incoming(struct lb_peer *lb_peer, uint32_t sccp_conn_id, const char *use_token);
struct lb_conn *lb_conn_create_outgoing(struct lb_peer *lb_peer, const char *use_token);
struct lb_conn *lb_conn_find_by_smlc_subscr(struct smlc_subscr *smlc_subscr, const char *use_token);

void lb_conn_msc_role_gone(struct lb_conn *lb_conn, struct osmo_fsm_inst *msc_role);
void lb_conn_close(struct lb_conn *lb_conn);
void lb_conn_discard(struct lb_conn *lb_conn);

int lb_conn_rx(struct lb_conn *lb_conn, struct msgb *msg, bool initial);
int lb_conn_send_bssmap_le(struct lb_conn *lb_conn, const struct bssmap_le_pdu *bssmap_le);
