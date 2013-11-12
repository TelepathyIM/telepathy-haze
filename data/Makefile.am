# Dbus service file
servicedir = $(datadir)/dbus-1/services
service_in_files = haze.service.in
service_DATA = im.telepathy.v1.ConnectionManager.haze.service

BUILT_FILES = $(service_DATA)

CLEANFILES = $(BUILT_FILES)

EXTRA_DIST = $(service_in_files)

# Rule to make the service file with libexecdir expanded
$(service_DATA): $(service_in_files) Makefile
	$(AM_V_GEN)sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@
