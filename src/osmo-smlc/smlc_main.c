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

#include <osmocom/ctrl/control_cmd.h>
#include <osmocom/ctrl/control_if.h>
#include <osmocom/ctrl/ports.h>
#include <osmocom/ctrl/control_vty.h>

#include <osmocom/core/application.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/signal.h>
#include <osmocom/core/stats.h>
#include <osmocom/core/rate_ctr.h>
#include <osmocom/vty/telnet_interface.h>
#include <osmocom/vty/ports.h>
#include <osmocom/vty/logging.h>
#include <osmocom/vty/command.h>

#include <osmocom/sigtran/xua_msg.h>
#include <osmocom/sigtran/sccp_sap.h>

#include <osmocom/smlc/smlc_data.h>
#include <osmocom/smlc/smlc_sigtran.h>

#define _GNU_SOURCE
#include <getopt.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>


#include "../../config.h"

static const char *config_file = "osmo-smlc.cfg";
static int daemonize = 0;
static void *tall_smlc_ctx;
struct smlc_state *g_smlc;

static void print_usage()
{
	printf("Usage: osmo-smlc\n");
}

static void print_help()
{
	printf("  Some useful help...\n");
	printf("  -h --help 			This text.\n");
	printf("  -D --daemonize 		Fork the process into a background daemon.\n");
	printf("  -d  --debug option 		--debug=DRLL:DMM:DRR:DRSL:DNM enable debugging.\n");
	printf("  -V --version               	Print the version of OsmoBSC.\n");
	printf("  -c --config-file filename	The config file to use.\n");
	printf("  -e --log-level number		Set a global loglevel.\n");
	printf("  --vty-ref-xml			Generate the VTY reference XML output and exit.\n");
}

static void handle_options(int argc, char **argv)
{
	while (1) {
		int option_index = 0, c;
		static int long_option = 0;
		static struct option long_options[] = {
			{"help", 0, 0, 'h'},
			{"debug", 1, 0, 'd'},
			{"daemonize", 0, 0, 'D'},
			{"config-file", 1, 0, 'c'},
			{"version", 0, 0, 'V' },
			{"log-level", 1, 0, 'e'},
			{"vty-ref-xml", 0, &long_option, 1},
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "hd:Dc:Ve:",
				long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			print_usage();
			print_help();
			exit(0);
		case 0:
			switch (long_option) {
			case 1:
				vty_dump_xml_ref(stdout);
				exit(0);
			default:
				fprintf(stderr, "error parsing cmdline options\n");
				exit(2);
			}
		case 'd':
			log_parse_category_mask(osmo_stderr_target, optarg);
			break;
		case 'D':
			daemonize = 1;
			break;
		case 'c':
			config_file = optarg;
			break;
		case 'V':
			print_version(1);
			exit(0);
			break;
		case 'e':
			log_set_log_level(osmo_stderr_target, atoi(optarg));
			break;
		default:
			/* catch unknown options *as well as* missing arguments. */
			fprintf(stderr, "Error in command line options. Exiting.\n");
			exit(-1);
		}
	}

	if (argc > optind) {
		fprintf(stderr, "Unsupported positional arguments on command line\n");
		exit(2);
	}
}

static struct vty_app_info vty_info = {
	.name 		= "OsmoSMLC",
	.copyright	=
	"Copyright (C) 2020 Harald Welte and sysmocom - s.f.m.c. GmbH\r\n\r\n"
	"License AGPLv3+: GNU AGPL version 3 or later <http://gnu.org/licenses/agpl-3.0.html>\r\n"
	"This is free software: you are free to change and redistribute it.\r\n"
	"There is NO WARRANTY, to the extent permitted by law.\r\n",
	.version	= PACKAGE_VERSION,
};

static void signal_handler(int signal)
{
	fprintf(stdout, "signal %u received\n", signal);

	switch (signal) {
	case SIGINT:
	case SIGTERM:
		exit(0);
		break;
	case SIGABRT:
		/* in case of abort, we want to obtain a talloc report
		 * and then return to the caller, who will abort the process */
	case SIGUSR1:
		talloc_report_full(tall_smlc_ctx, stderr);
		break;
	default:
		break;
	}
}

static const struct log_info_cat smlc_categories[] = {
};

const struct log_info log_info = {
	.cat = smlc_categories,
	.num_cat = ARRAY_SIZE(smlc_categories),
};

int main(int argc, char **argv)
{
	int rc;

	tall_smlc_ctx = talloc_named_const(NULL, 1, "osmo-smlc");
	msgb_talloc_ctx_init(tall_smlc_ctx, 0);
	osmo_signal_talloc_ctx_init(tall_smlc_ctx);
	osmo_xua_msg_tall_ctx_init(tall_smlc_ctx);
	vty_info.tall_ctx = tall_smlc_ctx;

	osmo_init_logging2(tall_smlc_ctx, &log_info);
	osmo_stats_init(tall_smlc_ctx);
	rate_ctr_init(tall_smlc_ctx);

	osmo_fsm_set_dealloc_ctx(OTC_SELECT);

	g_smlc = talloc_zero(tall_smlc_ctx, struct smlc_state);
	OSMO_ASSERT(g_smlc);

	/* This needs to precede handle_options() */
	vty_init(&vty_info);
	//smlc_vty_init(g_smlc);
	ctrl_vty_init(tall_smlc_ctx);

	/* Initialize SS7 */
	OSMO_ASSERT(osmo_ss7_init() == 0);
	osmo_ss7_vty_init_asp(tall_smlc_ctx);
	osmo_sccp_vty_init();

	/* parse options */
	handle_options(argc, argv);

	/* Read the config */
	rc = vty_read_config_file(config_file, NULL);
	if (rc < 0) {
		fprintf(stderr, "Failed to parse the config file: '%s'\n", config_file);
		exit(1);
	}

	/* Start telnet interface after reading config for vty_get_bind_addr() */
	rc = telnet_init_dynif(tall_smlc_ctx, g_smlc, vty_get_bind_addr(), OSMO_VTY_PORT_SMLC);
	if (rc < 0)
		exit(1);

	/* start control interface after reading config for
	 * ctrl_vty_get_bind_addr() */
	g_smlc->ctrl = ctrl_interface_setup_dynip2(g_smlc, ctrl_vty_get_bind_addr(), OSMO_CTRL_PORT_SMLC,
						   smlc_ctrl_node_lookup, _LAST_CTRL_NODE_SMLC);
	if (!g_smlc->ctrl) {
		fprintf(stderr, "Failed to init the control interface. Exiting.\n");
		exit(1);
	}

	/*
	rc = smlc_ctrl_cmds_install(g_smlc);
	if (rc < 0) {
		fprintf(stderr, "Failed to install control commands. Exiting.\n");
		exit(1);
	}
	*/

	if (smlc_sigtran_init() != 0) {
		LOGP(DLB, LOGL_ERROR, "Failed to initialize sigtran backhaul.\n");
		exit(1);
	}

	signal(SIGINT, &signal_handler);
	signal(SIGTERM, &signal_handler);
	signal(SIGABRT, &signal_handler);
	signal(SIGUSR1, &signal_handler);
	signal(SIGUSR2, &signal_handler);
	osmo_init_ignore_signals();

	if (daemonize) {
		rc = osmo_daemonize();
		if (rc < 0) {
			perror("Error during daemonize");
			exit(1);
		}
	}

	while (1) {
		osmo_select_main_ctx(0);
	}

	return 0;
}
