SUBDIRS = \
	examples \
	manuals \
	$(NULL)

msc: \
	$(builddir)/location_request_fsm.png \
	$(NULL)

dot: \
	$(NULL)

$(builddir)/%.png: $(srcdir)/%.msc
	mscgen -T png -o $@ $<

$(builddir)/%.png: $(srcdir)/%.dot
	dot -Tpng $< > $@

.PHONY: poll
poll:
	while true; do $(MAKE) msc dot; sleep 1; done
