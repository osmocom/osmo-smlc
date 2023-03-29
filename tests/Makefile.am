SUBDIRS = \
	smlc_subscr \
	$(NULL)

# The `:;' works around a Bash 3.2 bug when the output is not writeable.
$(srcdir)/package.m4: $(top_srcdir)/configure.ac
	:;{ \
               echo '# Signature of the current package.' && \
               echo 'm4_define([AT_PACKAGE_NAME],' && \
               echo '  [$(PACKAGE_NAME)])' && \
               echo 'm4_define([AT_PACKAGE_TARNAME],' && \
               echo '  [$(PACKAGE_TARNAME)])' && \
               echo 'm4_define([AT_PACKAGE_VERSION],' && \
               echo '  [$(PACKAGE_VERSION)])' && \
               echo 'm4_define([AT_PACKAGE_STRING],' && \
               echo '  [$(PACKAGE_STRING)])' && \
               echo 'm4_define([AT_PACKAGE_BUGREPORT],' && \
               echo '  [$(PACKAGE_BUGREPORT)])'; \
               echo 'm4_define([AT_PACKAGE_URL],' && \
               echo '  [$(PACKAGE_URL)])'; \
             } >'$(srcdir)/package.m4'

EXTRA_DIST = \
	testsuite.at \
	$(srcdir)/package.m4 \
	$(TESTSUITE) \
	test_nodes.vty \
	test_nodes.ctrl \
	cell_locations.vty \
	osmo-smlc.cfg \
	$(NULL)

TESTSUITE = $(srcdir)/testsuite

DISTCLEANFILES = \
	atconfig \
	$(NULL)

if ENABLE_EXT_TESTS
python-tests:
	$(MAKE) vty-test
	$(MAKE) ctrl-test
else
python-tests:
	echo "Not running python-based tests (determined at configure-time)"
endif

# Run a specific test with: 'make vty-test VTY_TEST=osmo-smlc.vty'
VTY_TEST ?= *.vty

# To update the VTY script from current application behavior,
# pass -u to vty_script_runner.py by doing:
#   make vty-test U=-u
vty-test: $(top_builddir)/src/osmo-smlc/osmo-smlc
	osmo_verify_transcript_vty.py -v \
		-n OsmoSMLC -p 4271 \
		-r "$(top_builddir)/src/osmo-smlc/osmo-smlc -c $(top_srcdir)/tests/osmo-smlc.cfg" \
		$(U) $(srcdir)/$(VTY_TEST)

# To update the CTRL script from current application behavior,
# pass -u to ctrl_script_runner.py by doing:
#   make ctrl-test U=-u
ctrl-test: $(top_builddir)/src/osmo-smlc/osmo-smlc
	-rm -f $(CTRL_TEST_DB)
	osmo_verify_transcript_ctrl.py -v \
		-p 4272 \
		-r "$(top_builddir)/src/osmo-smlc/osmo-smlc -c $(top_srcdir)/tests/osmo-smlc.cfg" \
		$(U) $(srcdir)/*.ctrl
	-rm -f $(CTRL_TEST_DB)
	-rm $(CTRL_TEST_DB)-*

check-local: atconfig $(TESTSUITE)
	$(SHELL) '$(TESTSUITE)' $(TESTSUITEFLAGS)
	$(MAKE) $(AM_MAKEFLAGS) python-tests

installcheck-local: atconfig $(TESTSUITE)
	$(SHELL) '$(TESTSUITE)' AUTOTEST_PATH='$(bindir)' \
		$(TESTSUITEFLAGS)

clean-local:
	test ! -f '$(TESTSUITE)' || \
		$(SHELL) '$(TESTSUITE)' --clean

AUTOM4TE = $(SHELL) $(top_srcdir)/missing --run autom4te
AUTOTEST = $(AUTOM4TE) --language=autotest
$(TESTSUITE): $(srcdir)/testsuite.at $(srcdir)/package.m4
	$(AUTOTEST) -I '$(srcdir)' -o $@.tmp $@.at
	mv $@.tmp $@
