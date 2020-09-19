#pragma once

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/rate_ctr.h>
#include <osmocom/core/stat_item.h>
#include <osmocom/core/tdef.h>

#include <osmocom/ctrl/control_if.h>

struct osmo_sccp_instance;
struct sccp_lb_inst;

struct smlc_state {
	struct osmo_sccp_instance *sccp_inst;
	struct sccp_lb_inst *lb;

	struct ctrl_handle *ctrl;

	struct rate_ctr_group *ctrs;
	struct osmo_stat_item_group *statg;

	struct llist_head subscribers;
	struct llist_head cell_locations;
};

extern struct smlc_state *g_smlc;
struct smlc_state *smlc_state_alloc(void *ctx);

extern struct osmo_tdef g_smlc_tdefs[];

int smlc_ctrl_node_lookup(void *data, vector vline, int *node_type,
			  void **node_data, int *i);

enum smlc_ctrl_node {
	CTRL_NODE_SMLC = _LAST_CTRL_NODE,
	_LAST_CTRL_NODE_SMLC
};

enum {
	SMLC_CTR_BSSMAP_LE_RX_UDT_RESET,
	SMLC_CTR_BSSMAP_LE_RX_UDT_RESET_ACK,
	SMLC_CTR_BSSMAP_LE_RX_UDT_ERR_INVALID_MSG,
	SMLC_CTR_BSSMAP_LE_RX_DT1_ERR_INVALID_MSG,
	SMLC_CTR_BSSMAP_LE_RX_DT1_PERFORM_LOCATION_REQUEST,
	SMLC_CTR_BSSMAP_LE_RX_DT1_BSSLAP_TA_RESPONSE,
	SMLC_CTR_BSSMAP_LE_RX_DT1_BSSLAP_REJECT,
	SMLC_CTR_BSSMAP_LE_RX_DT1_BSSLAP_RESET,
	SMLC_CTR_BSSMAP_LE_RX_DT1_BSSLAP_ABORT,

	SMLC_CTR_BSSMAP_LE_TX_ERR_INVALID_MSG,
	SMLC_CTR_BSSMAP_LE_TX_ERR_CONN_NOT_READY,
	SMLC_CTR_BSSMAP_LE_TX_ERR_SEND,
	SMLC_CTR_BSSMAP_LE_TX_SUCCESS,

	SMLC_CTR_BSSMAP_LE_TX_UDT_RESET,
	SMLC_CTR_BSSMAP_LE_TX_UDT_RESET_ACK,
	SMLC_CTR_BSSMAP_LE_TX_DT1_PERFORM_LOCATION_RESPONSE,
	SMLC_CTR_BSSMAP_LE_TX_DT1_BSSLAP_TA_REQUEST,
};
