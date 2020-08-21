#pragma once

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/rate_ctr.h>
#include <osmocom/core/stat_item.h>
#include <osmocom/core/tdef.h>

#include <osmocom/ctrl/control_if.h>

#include <osmocom/sigtran/sccp_sap.h>

struct smlc_state {
	struct osmo_sccp_user *sccp_user;
	struct ctrl_handle *ctrl;

	struct rate_ctr_group *ctrs;
	struct osmo_stat_item_group *statg;
	struct osmo_tdef *T_defs;
};

extern struct smlc_state *g_smlc;


enum {
	DSMLC,
	DLB,		/* Lb interface */
};


int smlc_ctrl_node_lookup(void *data, vector vline, int *node_type,
			  void **node_data, int *i);

enum smlc_ctrl_node {
	CTRL_NODE_SMLC = _LAST_CTRL_NODE,
	_LAST_CTRL_NODE_SMLC
};
