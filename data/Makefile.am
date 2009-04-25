include $(top_srcdir)/tools/shave.mk

# Telepathy manager file
managerdir = $(datadir)/telepathy/managers
dist_manager_DATA = haze.manager

# Dbus service file
servicedir = $(datadir)/dbus-1/services
service_in_files = org.freedesktop.Telepathy.ConnectionManager.haze.service.in
service_DATA = $(service_in_files:.service.in=.service)

BUILT_FILES = $(service_DATA)

CLEANFILES = $(BUILT_FILES)

EXTRA_DIST = $(service_in_files)

# Rule to make the service file with libexecdir expanded
$(service_DATA): $(service_in_files) Makefile
	$(QUIET_GEN)sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@
