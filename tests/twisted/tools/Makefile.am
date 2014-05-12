%.conf: %.conf.in Makefile
	$(AM_V_GEN)sed -e "s|[@]servicedir[@]|@abs_top_builddir@/tests/twisted/tools|g" $< > $@

servicedir = $(datadir)/dbus-1/services

installed/%.conf: %.conf.in Makefile
	@$(MKDIR_P) installed
	$(AM_V_GEN)sed -e "s|[@]servicedir[@]|$(servicedir)|g" $< > $@

# We don't use the full filename for the .in because > 99 character filenames
# in tarballs are non-portable (and automake 1.8 doesn't let us build
# non-archaic tarballs)
im.telepathy.v1.ConnectionManager.%.service: %.service.in
	$(AM_V_GEN)sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" \
		-e "s|[@]abs_top_srcdir[@]|@abs_top_srcdir@|g" $< > $@

# D-Bus service file for testing
service_in_files = haze.service.in
service_files = im.telepathy.v1.ConnectionManager.haze.service

# D-Bus config file for testing
conf_in_files = tmp-session-bus.conf.in
conf_files = $(conf_in_files:.conf.in=.conf)

BUILT_SOURCES = \
	$(built_test_data) \
	$(conf_files) \
	$(service_files) \
	$(NULL)

dist_test_scripts = \
	with-session-bus.sh \
	$(NULL)

dist_test_data = \
	$(NULL)

built_test_data = \
	installed/tmp-session-bus.conf \
	$(NULL)

EXTRA_DIST = \
	$(dist_test_data) \
	$(dist_test_scripts) \
	$(service_in_files) \
	$(conf_in_files) \
	exec-with-log.sh \
	$(NULL)

CLEANFILES = \
    $(BUILT_SOURCES) \
    haze-testing.log \
    $(NULL)

testsdir = ${datadir}/telepathy-haze-1-tests
twistedtestsdir = ${testsdir}/twisted
twistedtoolsdir = ${twistedtestsdir}/tools

if ENABLE_INSTALLED_TESTS
dist_twistedtools_SCRIPTS = ${dist_test_scripts}
dist_twistedtools_DATA = ${dist_test_data}
twistedtools_DATA = ${built_test_data}
endif
