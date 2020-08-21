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

#include <errno.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/fsm.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/sigtran/osmo_ss7.h>
#include <osmocom/sigtran/sccp_sap.h>
#include <osmocom/gsm/gsm0808.h>

#include <osmocom/smlc/smlc_data.h>
#include <osmocom/smlc/smlc_sigtran.h>


#define DEFAULT_M3UA_REMOTE_IP	"localhost"
#define DEFAULT_PC		"0.23.6"

static int sccp_sap_up(struct osmo_prim_hdr *oph, void *_scu)
{
	struct osmo_scu_prim *scu_prim = (struct osmo_scu_prim *)oph;
	//struct osmo_sccp_user *scu = _scu;
	int rc = 0;

	switch (OSMO_PRIM_HDR(&scu_prim->oph)) {
	case OSMO_PRIM(OSMO_SCU_PRIM_N_UNITDATA, PRIM_OP_INDICATION):
		/* Handle inbound UNITDATA */
		DEBUGP(DLB, "N-UNITDATA.ind(%s)\n", osmo_hexdump(msgb_l2(oph->msg), msgb_l2len(oph->msg)));
		//rc = handle_unitdata_from_bsc(&scu_prim->u.unitdata.calling_addr, oph->msg, scu);
		break;
	case OSMO_PRIM(OSMO_SCU_PRIM_N_CONNECT, PRIM_OP_INDICATION):
		/* Handle inbound connections */
		DEBUGP(DLB, "N-CONNECT.ind(X->%u)\n", scu_prim->u.connect.conn_id);
		break;
	case OSMO_PRIM(OSMO_SCU_PRIM_N_CONNECT, PRIM_OP_CONFIRM):
		/* Handle outbound connection confirmation */
		DEBUGP(DLB, "N-CONNECT.cnf(%u, %s)\n", scu_prim->u.connect.conn_id,
			osmo_hexdump(msgb_l2(oph->msg), msgb_l2len(oph->msg)));
		break;
	case OSMO_PRIM(OSMO_SCU_PRIM_N_DATA, PRIM_OP_INDICATION):
		/* Handle incoming connection oriented data */
		DEBUGP(DLB, "N-DATA.ind(%u, %s)\n", scu_prim->u.data.conn_id,
			osmo_hexdump(msgb_l2(oph->msg), msgb_l2len(oph->msg)));

		break;
	case OSMO_PRIM(OSMO_SCU_PRIM_N_DISCONNECT, PRIM_OP_INDICATION):
		DEBUGP(DLB, "N-DISCONNECT.ind(%u, %s, cause=%i)\n", scu_prim->u.disconnect.conn_id,
			osmo_hexdump(msgb_l2(oph->msg), msgb_l2len(oph->msg)),
			scu_prim->u.disconnect.cause);
		break;
	default:
		LOGP(DLB, LOGL_ERROR, "Unhandled SIGTRAN operation %s on primitive %u\n",
			get_value_string(osmo_prim_op_names, oph->operation), oph->primitive);
		break;
	}

	msgb_free(oph->msg);
	return rc;
}

int smlc_sigtran_init(void)
{
	struct osmo_sccp_instance *sccp;
	int default_pc = osmo_ss7_pointcode_parse(NULL, DEFAULT_PC);

	OSMO_ASSERT(default_pc);

	sccp = osmo_sccp_simple_client_on_ss7_id(g_smlc, 0, "Lb", default_pc, OSMO_SS7_ASP_PROT_M3UA,
						 0, NULL, 0, DEFAULT_M3UA_REMOTE_IP);


	g_smlc->sccp_user = osmo_sccp_user_bind(sccp, "SMLC", sccp_sap_up, OSMO_SCCP_SSN_SMLC_BSSAP);
	if (!g_smlc->sccp_user)
		return -EINVAL;

	return 0;
}
