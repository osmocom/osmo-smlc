EXTRA_DIST = \
    osmosmlc-usermanual.adoc \
    osmosmlc-usermanual-docinfo.xml \
    osmosmlc-vty-reference.xml \
    chapters \
    regen_doc.sh \
    vty

if BUILD_MANUALS
  ASCIIDOC = osmosmlc-usermanual.adoc
  include $(OSMO_GSM_MANUALS_DIR)/build/Makefile.asciidoc.inc
  osmosmlc-usermanual.pdf: $(srcdir)/chapters/*.adoc #$(srcdir)/chapters/*.dot

  VTY_REFERENCE = osmosmlc-vty-reference.xml

  BUILT_REFERENCE_XML = $(builddir)/vty/smlc_vty_reference.xml
  $(builddir)/vty/smlc_vty_reference.xml: $(top_builddir)/src/osmo-smlc/osmo-smlc
	mkdir -p $(builddir)/vty
	$(top_builddir)/src/osmo-smlc/osmo-smlc --vty-ref-xml > $@

  include $(OSMO_GSM_MANUALS_DIR)/build/Makefile.vty-reference.inc

  OSMO_REPOSITORY = osmo-smlc
  include $(OSMO_GSM_MANUALS_DIR)/build/Makefile.common.inc
endif
