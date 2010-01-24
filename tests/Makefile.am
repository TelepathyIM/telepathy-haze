SUBDIRS = .

if WANT_TWISTED_TESTS
SUBDIRS += twisted
endif

%.conf: %.conf.in
	$(AM_V_GEN)sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" $< > $@

# We don't use the full filename for the .in because > 99 character filenames
# in tarballs are non-portable (and automake 1.8 doesn't let us build
# non-archaic tarballs)
org.freedesktop.Telepathy.ConnectionManager.%.service: %.service.in
	$(AM_V_GEN)sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" \
		-e "s|[@]abs_top_srcdir[@]|@abs_top_srcdir@|g" $< > $@

# D-Bus service file for testing
service_in_files = haze.service.in
service_files = org.freedesktop.Telepathy.ConnectionManager.haze.service

# D-Bus config file for testing
conf_in_files = tmp-session-bus.conf.in
conf_files = $(conf_in_files:.conf.in=.conf)

BUILT_SOURCES = $(service_files) $(conf_files)

EXTRA_DIST = \
	$(service_in_files) \
	$(conf_in_files) \
	exec-with-log.sh

CLEANFILES = \
    $(BUILT_SOURCES) \
    haze-testing.log
