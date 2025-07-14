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
 */

#pragma once

#include <stdint.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/gsm/gsm0808_utils.h>

struct osmo_gad;

struct cell_location {
	struct llist_head entry;

	struct gsm0808_cell_id cell_id;

	/*! latitude in micro degrees (degrees * 1e6) */
	int32_t lat;
	/*! longitude in micro degrees (degrees * 1e6) */
	int32_t lon;
};

int cell_location_from_ta(struct osmo_gad *location_estimate,
			  const struct gsm0808_cell_id *cell_id,
			  uint8_t ta);

int cell_locations_vty_init();
