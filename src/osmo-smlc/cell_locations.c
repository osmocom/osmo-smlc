/* OsmoSMLC cell locations configuration */
/*
 * (C) 2020 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
 * All Rights Reserved
 *
 * Author: Neels Hofmeyr <neels@hofmeyr.de>
 *
 * SPDX-License-Identifier: GPL-2.0+
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <limits.h>
#include <inttypes.h>
#include <errno.h>

#include <osmocom/core/utils.h>
#include <osmocom/gsm/protocol/gsm_08_08.h>
#include <osmocom/gsm/gsm0808_utils.h>
#include <osmocom/gsm/gad.h>
#include <osmocom/smlc/smlc_data.h>
#include <osmocom/smlc/smlc_vty.h>
#include <osmocom/smlc/cell_locations.h>

static uint32_t ta_to_m(uint8_t ta)
{
	return ((uint32_t)ta) * 550;
}

static struct cell_location *cell_location_find(const struct gsm0808_cell_id *cell_id)
{
	struct cell_location *cell_location;
	llist_for_each_entry(cell_location, &g_smlc->cell_locations, entry) {
		if (gsm0808_cell_ids_match(&cell_location->cell_id, cell_id, true))
			return cell_location;
	}
	llist_for_each_entry(cell_location, &g_smlc->cell_locations, entry) {
		if (gsm0808_cell_ids_match(&cell_location->cell_id, cell_id, false))
			return cell_location;
	}
	return NULL;
}

int cell_location_from_ta(struct osmo_gad *location_estimate,
			  const struct gsm0808_cell_id *cell_id,
			  uint8_t ta)
{
	const struct cell_location *cell;
	cell = cell_location_find(cell_id);
	if (!cell)
		return -ENOENT;

	*location_estimate = (struct osmo_gad){
		.type = GAD_TYPE_ELL_POINT_UNC_CIRCLE,
		.ell_point_unc_circle = {
			.lat = cell->lat,
			.lon = cell->lon,
			.unc = osmo_gad_dec_unc(osmo_gad_enc_unc(ta_to_m(ta) * 1000)),
		},
	};

	return 0;
}

static struct cell_location *cell_location_find_or_create(const struct gsm0808_cell_id *cell_id)
{
	struct cell_location *cell_location = cell_location_find(cell_id);
	if (!cell_location) {
		cell_location = talloc_zero(g_smlc, struct cell_location);
		OSMO_ASSERT(cell_location);
		cell_location->cell_id = *cell_id;
		llist_add_tail(&cell_location->entry, &g_smlc->cell_locations);
	}
	return cell_location;

}

static const struct cell_location *cell_location_set(const struct gsm0808_cell_id *cell_id, int32_t lat, int32_t lon)
{
	struct cell_location *cell_location = cell_location_find_or_create(cell_id);
	cell_location->lat = lat;
	cell_location->lon = lon;
	return 0;
}

static int cell_location_remove(const struct gsm0808_cell_id *cell_id)
{
	struct cell_location *cell_location = cell_location_find(cell_id);
	if (!cell_location)
		return -ENOENT;
	llist_del(&cell_location->entry);
	talloc_free(cell_location);
	return 0;
}

#define LAC_CI_PARAMS "lac-ci <0-65535> <0-65535>"
#define LAC_CI_DOC "Cell location by LAC and CI\n" "LAC\n" "CI\n"

#define CGI_PARAMS "cgi <0-999> <0-999> <0-65535> <0-65535>"
#define CGI_DOC "Cell location by Cell-Global ID\n" "MCC\n" "MNC\n" "LAC\n" "CI\n"

#define LAT_LON_PARAMS "lat LATITUDE lon LONGITUDE"
#define LAT_LON_DOC "Global latitute coordinate\n" "Latitude floating-point number, -90.0 (S) to 90.0 (N)\n" \
		"Global longitude coordinate\n" "Longitude as floating-point number, -180.0 (W) to 180.0 (E)\n"

static int vty_parse_lac_ci(struct vty *vty, struct gsm0808_cell_id *dst, const char **argv)
{
	*dst = (struct gsm0808_cell_id){
		.id_discr = CELL_IDENT_LAC_AND_CI,
		.id.lac_and_ci = {
			.lac = atoi(argv[0]),
			.ci = atoi(argv[1]),
		},
	};
	return 0;
}

static int vty_parse_cgi(struct vty *vty, struct gsm0808_cell_id *dst, const char **argv)
{
	*dst = (struct gsm0808_cell_id){
		.id_discr = CELL_IDENT_WHOLE_GLOBAL,
	};
	struct osmo_cell_global_id *cgi = &dst->id.global;
	const char *mcc = argv[0];
	const char *mnc = argv[1];
	const char *lac = argv[2];
	const char *ci = argv[3];

	if (osmo_mcc_from_str(mcc, &cgi->lai.plmn.mcc)) {
		vty_out(vty, "%% Error decoding MCC: %s%s", mcc, VTY_NEWLINE);
		return -EINVAL;
	}

	if (osmo_mnc_from_str(mnc, &cgi->lai.plmn.mnc, &cgi->lai.plmn.mnc_3_digits)) {
		vty_out(vty, "%% Error decoding MNC: %s%s", mnc, VTY_NEWLINE);
		return -EINVAL;
	}

	cgi->lai.lac = atoi(lac);
	cgi->cell_identity = atoi(ci);
	return 0;
}

static int vty_parse_location(struct vty *vty, const struct gsm0808_cell_id *cell_id, const char **argv)
{
	const char *lat_str = argv[0];
	const char *lon_str = argv[1];
	int64_t val;
	int32_t lat, lon;

	if (osmo_float_str_to_int(&val, lat_str, 6)
	    || val < -90000000 || val > 90000000) {
		vty_out(vty, "%% Invalid latitude: '%s'%s", lat_str, VTY_NEWLINE);
		return CMD_WARNING;
	}
	lat = val;

	if (osmo_float_str_to_int(&val, lon_str, 6)
	    || val < -180000000 || val > 180000000) {
		vty_out(vty, "%% Invalid longitude: '%s'%s", lon_str, VTY_NEWLINE);
		return CMD_WARNING;
	}
	lon = val;

	if (cell_location_set(cell_id, lat, lon)) {
		vty_out(vty, "%% Failed to add cell location%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	return CMD_SUCCESS;
}

DEFUN(cfg_cells, cfg_cells_cmd,
      "cells",
      "Configure cell locations\n")
{
	vty->node = CELLS_NODE;
	return CMD_SUCCESS;
}

DEFUN(cfg_cells_lac_ci, cfg_cells_lac_ci_cmd,
      LAC_CI_PARAMS " " LAT_LON_PARAMS,
      LAC_CI_DOC LAT_LON_DOC)
{
	struct gsm0808_cell_id cell_id;

	if (vty_parse_lac_ci(vty, &cell_id, argv))
		return CMD_WARNING;

	return vty_parse_location(vty, &cell_id, argv + 2);
}

DEFUN(cfg_cells_no_lac_ci, cfg_cells_no_lac_ci_cmd,
      "no " LAC_CI_PARAMS,
      NO_STR "Remove " LAC_CI_DOC)
{
	struct gsm0808_cell_id cell_id;

	if (vty_parse_lac_ci(vty, &cell_id, argv))
		return CMD_WARNING;
	if (cell_location_remove(&cell_id)) {
		vty_out(vty, "%% cannot remove, no such entry%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	return CMD_SUCCESS;
}

DEFUN(cfg_cells_cgi, cfg_cells_cgi_cmd,
      CGI_PARAMS " " LAT_LON_PARAMS,
      CGI_DOC LAT_LON_DOC)
{
	struct gsm0808_cell_id cell_id;

	if (vty_parse_cgi(vty, &cell_id, argv))
		return CMD_WARNING;

	return vty_parse_location(vty, &cell_id, argv + 4);
}

DEFUN(cfg_cells_no_cgi, cfg_cells_no_cgi_cmd,
      "no " CGI_PARAMS,
      NO_STR "Remove " CGI_DOC)
{
	struct gsm0808_cell_id cell_id;

	if (vty_parse_cgi(vty, &cell_id, argv))
		return CMD_WARNING;
	if (cell_location_remove(&cell_id)) {
		vty_out(vty, "%% cannot remove, no such entry%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	return CMD_SUCCESS;
}

/* The above are omnidirectional cells. If we add configuration sector antennae, it would add arguments to the above,
 * something like this:
 *  cgi 001 01 23 42 lat 23.23 lon 42.42 arc 270 30
 */

struct cmd_node cells_node = {
	CELLS_NODE,
	"%s(config-cells)# ",
	1,
};

static int config_write_cells(struct vty *vty)
{
	struct cell_location *cell;
	const struct osmo_cell_global_id *cgi;

	if (llist_empty(&g_smlc->cell_locations))
		return 0;

	vty_out(vty, "cells%s", VTY_NEWLINE);

	llist_for_each_entry(cell, &g_smlc->cell_locations, entry) {
		switch (cell->cell_id.id_discr) {
		case CELL_IDENT_LAC_AND_CI:
			vty_out(vty, " lac-ci %u %u", cell->cell_id.id.lac_and_ci.lac, cell->cell_id.id.lac_and_ci.ci);
			break;
		case CELL_IDENT_WHOLE_GLOBAL:
			cgi = &cell->cell_id.id.global;
			vty_out(vty, " cgi %s %s %u %u",
				osmo_mcc_name(cgi->lai.plmn.mcc),
				osmo_mnc_name(cgi->lai.plmn.mnc, cgi->lai.plmn.mnc_3_digits),
				cgi->lai.lac, cgi->cell_identity);
			break;
		default:
			vty_out(vty, " %% [unsupported cell id type: %d]",
				cell->cell_id.id_discr);
			break;
		}

		vty_out(vty, " lat %s lon %s%s",
			osmo_int_to_float_str_c(OTC_SELECT, cell->lat, 6),
			osmo_int_to_float_str_c(OTC_SELECT, cell->lon, 6),
			VTY_NEWLINE);
	}

	return 0;
}

DEFUN(ve_show_cells, ve_show_cells_cmd,
      "show cells",
      SHOW_STR "Show configured cell locations\n")
{
	if (llist_empty(&g_smlc->cell_locations)) {
		vty_out(vty, "%% No cell locations are configured%s", VTY_NEWLINE);
		return CMD_SUCCESS;
	}
	config_write_cells(vty);
	return CMD_SUCCESS;
}

int cell_locations_vty_init()
{
	install_element(CONFIG_NODE, &cfg_cells_cmd);
	install_node(&cells_node, config_write_cells);
	install_element(CELLS_NODE, &cfg_cells_lac_ci_cmd);
	install_element(CELLS_NODE, &cfg_cells_no_lac_ci_cmd);
	install_element(CELLS_NODE, &cfg_cells_cgi_cmd);
	install_element(CELLS_NODE, &cfg_cells_no_cgi_cmd);
	install_element_ve(&ve_show_cells_cmd);

	return 0;
}
