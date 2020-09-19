/* Lb: BSSAP-LE/SCCP */

#pragma once

#include <stdint.h>

#include <osmocom/core/tdef.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/gsm0808_utils.h>
#include <osmocom/sigtran/sccp_sap.h>

struct msgb;
struct sccp_lb_inst;

#define LOG_SCCP_LB_CO(sli, peer_addr, conn_id, level, fmt, args...) \
	LOGP(DLB, level, "(Lb-%u%s%s) " fmt, \
	     conn_id, peer_addr ? " from " : "", \
	     peer_addr ? osmo_sccp_inst_addr_name((sli)->sccp, peer_addr) : "", \
	     ## args)

#define LOG_SCCP_LB_CL_CAT(sli, peer_addr, subsys, level, fmt, args...) \
	LOGP(subsys, level, "(Lb%s%s) " fmt, \
	     peer_addr ? " from " : "", \
	     peer_addr ? osmo_sccp_inst_addr_name((sli)->sccp, peer_addr) : "", \
	     ## args)

#define LOG_SCCP_LB_CL(sli, peer_addr, level, fmt, args...) \
	LOG_SCCP_LB_CL_CAT(sli, peer_addr, DLB, level, fmt, ##args)

#define LOG_SCCP_LB_CAT(sli, subsys, level, fmt, args...) \
	LOG_SCCP_LB_CL_CAT(sli, NULL, subsys, level, fmt, ##args)

#define LOG_SCCP_LB(sli, level, fmt, args...) \
	LOG_SCCP_LB_CL(sli, NULL, level, fmt, ##args)

enum reset_msg_type {
	SCCP_LB_MSG_NON_RESET = 0,
	SCCP_LB_MSG_RESET,
	SCCP_LB_MSG_RESET_ACK,
};

struct sccp_lb_inst {
	struct osmo_sccp_instance *sccp;
	struct osmo_sccp_user *scu;
	struct osmo_sccp_addr local_sccp_addr;

	struct llist_head lb_peers;
	struct llist_head lb_conns;

	void *user_data;
};

struct sccp_lb_inst *sccp_lb_init(void *talloc_ctx, struct osmo_sccp_instance *sccp, enum osmo_sccp_ssn ssn,
				  const char *sccp_user_name);
int sccp_lb_inst_next_conn_id();

int sccp_lb_down_l2_co_initial(struct sccp_lb_inst *sli,
			       const struct osmo_sccp_addr *called_addr,
			       uint32_t conn_id, struct msgb *l2);
int sccp_lb_down_l2_co(struct sccp_lb_inst *sli, uint32_t conn_id, struct msgb *l2);
int sccp_lb_down_l2_cl(struct sccp_lb_inst *sli, const struct osmo_sccp_addr *called_addr, struct msgb *l2);

int sccp_lb_disconnect(struct sccp_lb_inst *sli, uint32_t conn_id, uint32_t cause);
