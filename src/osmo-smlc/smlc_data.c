/* (C) 2020 by Harald Welte <laforge@gnumonks.org>
 * All Rights Reserved
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
 * along with this program.  If not, see <http://www.gnu.org/lienses/>.
 *
 */

#include <osmocom/core/stats.h>
#include <osmocom/smlc/smlc_data.h>

struct osmo_tdef g_smlc_tdefs[] = {
	{ .T=-12, .default_val=5, .desc="Timeout for BSSLAP TA Response from BSC" },
	{}
};

static const struct rate_ctr_desc smlc_ctr_description[] = {
	[SMLC_CTR_BSSMAP_LE_RX_UDT_RESET] =	{ "bssmap_le:rx_udt_reset", "Rx BSSMAP-LE Reset" },
	[SMLC_CTR_BSSMAP_LE_RX_UDT_RESET_ACK] =	{ "bssmap_le:rx_udt_reset_ack", "Rx BSSMAP-LE Reset Acknowledge" },
	[SMLC_CTR_BSSMAP_LE_RX_UDT_ERR_INVALID_MSG] =	{ "bssmap_le:rx_udt_err_invalid_msg", "Receive invalid UnitData message" },
	[SMLC_CTR_BSSMAP_LE_RX_DT1_ERR_INVALID_MSG] =	{ "bssmap_le:rx_dt1_err_invalid_msg", "Receive invalid DirectTransfer1 message" },
	[SMLC_CTR_BSSMAP_LE_RX_DT1_PERFORM_LOCATION_REQUEST] =	{ "bssmap_le:rx_dt1_perform_location_request", "Receive Perform Location Request from BSC" },
	[SMLC_CTR_BSSMAP_LE_RX_DT1_BSSLAP_TA_RESPONSE] =	{ "bssmap_le:rx_dt1_bsslap_ta_response", "Receive BSSLAP TA Response from BSC" },
	[SMLC_CTR_BSSMAP_LE_RX_DT1_BSSLAP_REJECT] =	{ "bssmap_le:rx_dt1_bsslap_reject", "Rx BSSLAP Reject from BSC" },
	[SMLC_CTR_BSSMAP_LE_RX_DT1_BSSLAP_RESET] =	{ "bssmap_le:rx_dt1_bsslap_reset", "Rx BSSLAP Reset (handover) from BSC" },
	[SMLC_CTR_BSSMAP_LE_RX_DT1_BSSLAP_ABORT] =	{ "bssmap_le:rx_dt1_bsslap_abort", "Rx BSSLAP Abort from BSC" },

	[SMLC_CTR_BSSMAP_LE_TX_ERR_INVALID_MSG] =	{ "bssmap_le:tx_err_invalid_msg", "BSSMAP-LE send error: invalid message" },
	[SMLC_CTR_BSSMAP_LE_TX_ERR_CONN_NOT_READY] =	{ "bssmap_le:tx_err_conn_not_ready", "BSSMAP-LE send error: conn not ready" },
	[SMLC_CTR_BSSMAP_LE_TX_ERR_SEND] =	{ "bssmap_le:tx_err_send", "BSSMAP-LE send error" },
	[SMLC_CTR_BSSMAP_LE_TX_SUCCESS] =	{ "bssmap_le:tx_success", "BSSMAP-LE send success" },

	[SMLC_CTR_BSSMAP_LE_TX_UDT_RESET] =	{ "bssmap_le:tx_udt_reset", "Transmit UnitData Reset" },
	[SMLC_CTR_BSSMAP_LE_TX_UDT_RESET_ACK] =	{ "bssmap_le:tx_udt_reset_ack", "Transmit UnitData Reset Acknowledge" },
	[SMLC_CTR_BSSMAP_LE_TX_DT1_PERFORM_LOCATION_RESPONSE] =	{ "bssmap_le:tx_dt1_perform_location_response", "Tx Perform Location Response to BSC" },
	[SMLC_CTR_BSSMAP_LE_TX_DT1_BSSLAP_TA_REQUEST] =	{ "bssmap_le:tx_dt1_bsslap_ta_request", "Tx BSSLAP TA Request to BSC" },
};

static const struct rate_ctr_group_desc smlc_ctrg_desc = {
	"smlc",
	"serving mobile location center",
	OSMO_STATS_CLASS_GLOBAL,
	ARRAY_SIZE(smlc_ctr_description),
	smlc_ctr_description,
};

static const struct osmo_stat_item_desc smlc_stat_item_description[] = {
	[SMLC_STAT_LB_PEERS_TOTAL]	= { "lb_peers.total", "Total Lb peers seen since startup", OSMO_STAT_ITEM_NO_UNIT, 4, 0},
	[SMLC_STAT_LB_PEERS_ACTIVE]	= { "lb_peers.active", "Currently active Lb peers", OSMO_STAT_ITEM_NO_UNIT, 4, 0},
};

static const struct osmo_stat_item_group_desc smlc_statg_desc = {
	"smlc",
	"serving mobile location center",
	OSMO_STATS_CLASS_GLOBAL,
	ARRAY_SIZE(smlc_stat_item_description),
	smlc_stat_item_description,
};

struct smlc_state *smlc_state_alloc(void *ctx)
{
	struct smlc_state *smlc = talloc_zero(ctx, struct smlc_state);
	OSMO_ASSERT(smlc);
	INIT_LLIST_HEAD(&smlc->subscribers);
	INIT_LLIST_HEAD(&smlc->cell_locations);
	smlc->ctrs = rate_ctr_group_alloc(smlc, &smlc_ctrg_desc, 0);

	smlc->statg = osmo_stat_item_group_alloc(smlc, &smlc_statg_desc, 0);
	if (!smlc->statg)
		goto ret_free;
	return smlc;

ret_free:
	rate_ctr_group_free(smlc->ctrs);
	talloc_free(smlc);
	return NULL;
}
