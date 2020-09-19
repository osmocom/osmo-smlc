/* GSM subscriber details for use in SMLC */
/*
 * (C) 2020 by sysmocom s.f.m.c. GmbH <info@sysmocom.de>
 *
 * Author: Neels Hofmeyr <neels@hofmeyr.de>
 *
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <osmocom/smlc/debug.h>
#include <osmocom/smlc/smlc_data.h>
#include <osmocom/smlc/smlc_subscr.h>

static void smlc_subscr_free(struct smlc_subscr *smlc_subscr)
{
	llist_del(&smlc_subscr->entry);
	talloc_free(smlc_subscr);
}

static int smlc_subscr_use_cb(struct osmo_use_count_entry *e, int32_t old_use_count, const char *file, int line)
{
	struct smlc_subscr *smlc_subscr = e->use_count->talloc_object;
	int32_t total;
	int level;

	if (!e->use)
		return -EINVAL;

	total = osmo_use_count_total(&smlc_subscr->use_count);

	if (total == 0
	    || (total == 1 && old_use_count == 0 && e->count == 1))
		level = LOGL_INFO;
	else
		level = LOGL_DEBUG;

	LOGPSRC(DREF, level, file, line, "%s: %s %s\n",
		smlc_subscr_to_str_c(OTC_SELECT, smlc_subscr),
		(e->count - old_use_count) > 0? "+" : "-", e->use);

	if (e->count < 0)
		return -ERANGE;

	if (total == 0)
		smlc_subscr_free(smlc_subscr);
	return 0;
}

static struct smlc_subscr *smlc_subscr_alloc()
{
	struct smlc_subscr *smlc_subscr;

	smlc_subscr = talloc_zero(g_smlc, struct smlc_subscr);
	if (!smlc_subscr)
		return NULL;

	smlc_subscr->use_count = (struct osmo_use_count){
		.talloc_object = smlc_subscr,
		.use_cb = smlc_subscr_use_cb,
	};

	llist_add_tail(&smlc_subscr->entry, &g_smlc->subscribers);

	return smlc_subscr;
}

struct smlc_subscr *smlc_subscr_find(const struct osmo_mobile_identity *imsi, const char *use_token)
{
	struct smlc_subscr *smlc_subscr;
	if (!imsi)
		return NULL;

	llist_for_each_entry(smlc_subscr, &g_smlc->subscribers, entry) {
		if (!osmo_mobile_identity_cmp(&smlc_subscr->imsi, imsi)) {
			smlc_subscr_get(smlc_subscr, use_token);
			return smlc_subscr;
		}
	}
	return NULL;
}

struct smlc_subscr *smlc_subscr_find_or_create(const struct osmo_mobile_identity *imsi, const char *use_token)
{
	struct smlc_subscr *smlc_subscr;
	if (!imsi)
		return NULL;
	smlc_subscr = smlc_subscr_find(imsi, use_token);
	if (smlc_subscr)
		return smlc_subscr;
	smlc_subscr = smlc_subscr_alloc();
	if (!smlc_subscr)
		return NULL;
	smlc_subscr->imsi = *imsi;
	smlc_subscr_get(smlc_subscr, use_token);
	return smlc_subscr;
}

int smlc_subscr_to_str_buf(char *buf, size_t buf_len, const struct smlc_subscr *smlc_subscr)
{
	struct osmo_strbuf sb = { .buf = buf, .len = buf_len };
	OSMO_STRBUF_APPEND(sb, osmo_mobile_identity_to_str_buf, &smlc_subscr->imsi);
	OSMO_STRBUF_PRINTF(sb, "[");
	OSMO_STRBUF_APPEND(sb, osmo_use_count_to_str_buf, &smlc_subscr->use_count);
	OSMO_STRBUF_PRINTF(sb, "]");
	return sb.chars_needed;
}

char *smlc_subscr_to_str_c(void *ctx, const struct smlc_subscr *smlc_subscr)
{
	OSMO_NAME_C_IMPL(ctx, 64, "ERROR", smlc_subscr_to_str_buf, smlc_subscr)
}
