#pragma once

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/fsm.h>
#include <osmocom/core/use_count.h>
#include <osmocom/gsm/gsm48.h>
#include <osmocom/gsm/gsm0808.h>

struct smlc_subscr {
	struct llist_head entry;
	struct osmo_use_count use_count;

	struct osmo_mobile_identity imsi;
	struct gsm0808_cell_id cell_id;

	struct osmo_fsm_inst *loc_req;
};

struct smlc_subscr *smlc_subscr_find_or_create(const struct osmo_mobile_identity *imsi, const char *use_token);
struct smlc_subscr *smlc_subscr_find(const struct osmo_mobile_identity *imsi, const char *use_token);

int smlc_subscr_to_str_buf(char *buf, size_t buf_len, const struct smlc_subscr *smlc_subscr);
char *smlc_subscr_to_str_c(void *ctx, const struct smlc_subscr *smlc_subscr);

struct smlc_subscr *smlc_subscr_find_or_create(const struct osmo_mobile_identity *imsi, const char *use_token);

#define smlc_subscr_get(smlc_subscr, use) \
	OSMO_ASSERT(osmo_use_count_get_put(&(smlc_subscr)->use_count, use, 1) == 0)
#define smlc_subscr_put(smlc_subscr, use) \
	OSMO_ASSERT(osmo_use_count_get_put(&(smlc_subscr)->use_count, use, -1) == 0)
