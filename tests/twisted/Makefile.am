SUBDIRS = tools

TWISTED_TESTS = \
	avatar-requirements.py \
	simple-caps.py \
	cm/protocols.py \
	connect/fail.py \
	connect/success.py \
	connect/twice-to-same-account.py \
	presence/presence.py \
	roster/initial-roster.py \
	roster/groups.py \
	roster/publish.py \
	roster/removed-from-rp-subscribe.py \
	roster/subscribe.py \
	sasl/close.py \
	sasl/telepathy-password.py \
	text/destroy.py \
	text/ensure.py \
	text/initiate-requestotron.py \
	text/initiate.py \
	text/respawn.py \
	text/test-text-delayed.py \
	text/test-text-no-body.py \
	text/test-text.py


check-local: check-twisted

CHECK_TWISTED_SLEEP=0

check-twisted: $(BUILT_SOURCES)
	$(MAKE) -C tools
	if test "x$(CHECK_TWISTED_SLEEP)" = x0; then \
		haze_test_sleep= ; \
	else \
		haze_test_sleep=--sleep=$(CHECK_TWISTED_SLEEP); \
	fi; \
	HAZE_TEST_UNINSTALLED=1 \
	  HAZE_ABS_TOP_SRCDIR=@abs_top_srcdir@ \
	  HAZE_ABS_TOP_BUILDDIR=@abs_top_builddir@ \
	  HAZE_TEST_SLEEP=$$haze_test_sleep \
	  ./run-test.sh "$(TWISTED_TESTS)"

EXTRA_DIST = \
	$(TWISTED_TESTS) \
	constants.py \
	gabbletest.py \
	hazetest.py \
	run-test.sh.in \
	sasl/saslutil.py \
	servicetest.py \
	ns.py

if MEDIA_ENABLED
MEDIA_ENABLED_PYBOOL = True
else
MEDIA_ENABLED_PYBOOL = False
endif

config.py: Makefile
	$(AM_V_GEN) { \
		echo "MEDIA_ENABLED = $(MEDIA_ENABLED_PYBOOL)"; \
	} > $@

haze-twisted-tests.list: Makefile
	$(AM_V_GEN)echo $(TWISTED_TESTS) > $@

BUILT_SOURCES = \
       config.py \
       haze-twisted-tests.list \
       run-test.sh \
       $(NULL)

# We don't really use hazetestsdir yet - we only support uninstalled testing
# so far - but I'm substituting it to keep the script more similar to Gabble's.
# ${pkglibexecdir}/tests is what GNOME's InstalledTests goal recommends.
run-test.sh: run-test.sh.in Makefile
	$(AM_V_GEN)sed \
			-e 's![@]hazetestsdir[@]!${pkglibexecdir}/tests!' \
			-e 's![@]TEST_PYTHON[@]!$(TEST_PYTHON)!' \
			< $< > $@.tmp && \
		chmod +x $@.tmp && \
		mv $@.tmp $@

CLEANFILES = \
	$(BUILT_SOURCES) \
	haze-[1-9]*.log \
	*.pyc \
	*/*.pyc \
	$(NULL)
