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

#include <osmocom/core/application.h>
#include <osmocom/core/utils.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

struct smlc_state *g_smlc;

#define VERBOSE_ASSERT(val, expect_op, fmt) \
	do { \
		printf(#val " == " fmt "\n", (val)); \
		OSMO_ASSERT((val) expect_op); \
	} while (0);

#define USE_FOO "foo"
#define USE_BAR "bar"

static void assert_smlc_subscr(const struct smlc_subscr *smlc_subscr, const struct osmo_mobile_identity *imsi)
{
	struct smlc_subscr *sfound;
	OSMO_ASSERT(smlc_subscr);
	OSMO_ASSERT(osmo_mobile_identity_cmp(&smlc_subscr->imsi, imsi) == 0);

	sfound = smlc_subscr_find(imsi, __func__);
	OSMO_ASSERT(sfound == smlc_subscr);

	smlc_subscr_put(sfound, __func__);
}

static void test_smlc_subscr(void)
{
	struct smlc_subscr *s1, *s2, *s3;
	const struct osmo_mobile_identity imsi1 = { .type = GSM_MI_TYPE_IMSI, .imsi = "1234567890", };
	const struct osmo_mobile_identity imsi2 = { .type = GSM_MI_TYPE_IMSI, .imsi = "9876543210", };
	const struct osmo_mobile_identity imsi3 = { .type = GSM_MI_TYPE_IMSI, .imsi = "423423", };

	printf("Test SMLC subscriber allocation and deletion\n");

	/* Check for emptiness */
	VERBOSE_ASSERT(llist_count(&g_smlc->subscribers), == 0, "%d");
	OSMO_ASSERT(smlc_subscr_find(&imsi1, "-") == NULL);
	OSMO_ASSERT(smlc_subscr_find(&imsi2, "-") == NULL);
	OSMO_ASSERT(smlc_subscr_find(&imsi3, "-") == NULL);

	/* Allocate entry 1 */
	s1 = smlc_subscr_find_or_create(&imsi1, USE_FOO);
	VERBOSE_ASSERT(llist_count(&g_smlc->subscribers), == 1, "%d");
	assert_smlc_subscr(s1, &imsi1);
	VERBOSE_ASSERT(llist_count(&g_smlc->subscribers), == 1, "%d");
	OSMO_ASSERT(smlc_subscr_find(&imsi2, "-") == NULL);

	/* Allocate entry 2 */
	s2 = smlc_subscr_find_or_create(&imsi2, USE_BAR);
	VERBOSE_ASSERT(llist_count(&g_smlc->subscribers), == 2, "%d");

	/* Allocate entry 3 */
	s3 = smlc_subscr_find_or_create(&imsi3, USE_FOO);
	smlc_subscr_get(s3, USE_BAR);
	VERBOSE_ASSERT(llist_count(&g_smlc->subscribers), == 3, "%d");

	/* Check entries */
	assert_smlc_subscr(s1, &imsi1);
	assert_smlc_subscr(s2, &imsi2);
	assert_smlc_subscr(s3, &imsi3);

	/* Free entry 1 */
	smlc_subscr_put(s1, USE_FOO);
	s1 = NULL;
	VERBOSE_ASSERT(llist_count(&g_smlc->subscribers), == 2, "%d");
	OSMO_ASSERT(smlc_subscr_find(&imsi1, "-") == NULL);

	assert_smlc_subscr(s2, &imsi2);
	assert_smlc_subscr(s3, &imsi3);

	/* Free entry 2 */
	smlc_subscr_put(s2, USE_BAR);
	s2 = NULL;
	VERBOSE_ASSERT(llist_count(&g_smlc->subscribers), == 1, "%d");
	OSMO_ASSERT(smlc_subscr_find(&imsi1, "-") == NULL);
	OSMO_ASSERT(smlc_subscr_find(&imsi2, "-") == NULL);
	assert_smlc_subscr(s3, &imsi3);

	/* Remove one use of entry 3 */
	smlc_subscr_put(s3, USE_BAR);
	assert_smlc_subscr(s3, &imsi3);
	VERBOSE_ASSERT(llist_count(&g_smlc->subscribers), == 1, "%d");

	/* Free entry 3 */
	smlc_subscr_put(s3, USE_FOO);
	s3 = NULL;
	VERBOSE_ASSERT(llist_count(&g_smlc->subscribers), == 0, "%d");
	OSMO_ASSERT(smlc_subscr_find(&imsi3, "-") == NULL);

	OSMO_ASSERT(llist_empty(&g_smlc->subscribers));
}

static const struct log_info_cat log_categories[] = {
	[DREF] = {
		.name = "DREF",
		.description = "Reference Counting",
		.enabled = 1, .loglevel = LOGL_DEBUG,
	},
};

static const struct log_info log_info = {
	.cat = log_categories,
	.num_cat = ARRAY_SIZE(log_categories),
};

int main()
{
	void *ctx = talloc_named_const(NULL, 0, "smlc_subscr_test");

	osmo_init_logging2(ctx, &log_info);
	log_set_print_filename(osmo_stderr_target, 0);
	log_set_print_timestamp(osmo_stderr_target, 0);
	log_set_use_color(osmo_stderr_target, 0);
	log_set_print_category(osmo_stderr_target, 1);

	g_smlc = smlc_state_alloc(ctx);

	printf("Testing SMLC subscriber code.\n");

	test_smlc_subscr();

	printf("Done\n");
	return 0;
}

