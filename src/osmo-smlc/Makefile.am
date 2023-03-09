AM_CPPFLAGS = \
	$(all_includes) \
	-I$(top_srcdir)/include \
	-I$(top_builddir) \
	$(NULL)

AM_CFLAGS = \
	-Wall \
	$(LIBOSMOCORE_CFLAGS) \
	$(LIBOSMOGSM_CFLAGS) \
	$(LIBOSMOVTY_CFLAGS) \
	$(LIBOSMOCTRL_CFLAGS) \
	$(COVERAGE_CFLAGS) \
	$(LIBOSMOSIGTRAN_CFLAGS) \
	$(NULL)

AM_LDFLAGS = \
	$(COVERAGE_LDFLAGS) \
	$(NULL)

bin_PROGRAMS = \
	osmo-smlc \
	$(NULL)

osmo_smlc_SOURCES = \
	cell_locations.c \
	lb_conn.c \
	lb_peer.c \
	sccp_lb_inst.c \
	smlc_ctrl.c \
	smlc_data.c \
	smlc_loc_req.c \
	smlc_main.c \
	smlc_subscr.c \
	$(NULL)

osmo_smlc_LDADD = \
	$(LIBOSMOCORE_LIBS) \
	$(LIBOSMOGSM_LIBS) \
	$(LIBOSMOVTY_LIBS) \
	$(LIBOSMOCTRL_LIBS) \
	$(LIBOSMOSIGTRAN_LIBS) \
	$(NULL)
