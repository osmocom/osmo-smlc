/*
 * (C) 2025 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
 * All Rights Reserved
 *
 * SPDX-License-Identifier: AGPL-3.0+
 *
 * Author: Pau Espin Pedrol
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

#include <osmocom/ctrl/control_vty.h>
#include <osmocom/vty/logging.h>
#include <osmocom/vty/misc.h>
#include <osmocom/vty/stats.h>
#include <osmocom/vty/vty.h>

#include <osmocom/sigtran/osmo_ss7.h>
#include <osmocom/sigtran/sccp_sap.h>

#include <osmocom/smlc/cell_locations.h>

void smlc_vty_init(struct vty_app_info *vty_app_info)
{
	vty_init(vty_app_info);

	logging_vty_add_cmds();
	osmo_talloc_vty_add_cmds();
	ctrl_vty_init(vty_app_info->tall_ctx);
	osmo_fsm_vty_add_cmds();
	osmo_stats_vty_add_cmds();

	osmo_ss7_vty_init_asp(vty_app_info->tall_ctx);
	osmo_sccp_vty_init();

	cell_locations_vty_init();
}
