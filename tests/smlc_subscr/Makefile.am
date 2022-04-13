AM_CPPFLAGS = \
	$(all_includes) \
	-I$(top_srcdir)/include \
	$(NULL)

AM_CFLAGS = \
	-Wall \
	-ggdb3 \
	$(LIBOSMOCORE_CFLAGS) \
	$(LIBOSMOGSM_CFLAGS) \
	$(COVERAGE_CFLAGS) \
	$(NULL)

AM_LDFLAGS = \
	$(COVERAGE_LDFLAGS) \
	$(NULL)

EXTRA_DIST = \
	smlc_subscr_test.ok \
	smlc_subscr_test.err \
	$(NULL)

check_PROGRAMS = \
	smlc_subscr_test \
	$(NULL)

smlc_subscr_test_SOURCES = \
	smlc_subscr_test.c \
	$(NULL)

smlc_subscr_test_LDADD = \
	$(top_builddir)/src/osmo-smlc/smlc_data.o \
	$(top_builddir)/src/osmo-smlc/smlc_subscr.o \
	$(LIBOSMOCORE_LIBS) \
	$(LIBOSMOGSM_LIBS) \
	$(NULL)

update_exp:
	$(builddir)/smlc_subscr_test >$(srcdir)/smlc_subscr_test.ok 2>$(srcdir)/smlc_subscr_test.err
