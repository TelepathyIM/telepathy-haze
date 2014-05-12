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
	text/respawn.py \
	text/test-text-delayed.py \
	text/test-text-no-body.py \
	text/test-text.py

if WANT_TWISTED_TESTS
check-local: check-twisted
endif

CHECK_TWISTED_SLEEP=0

check-twisted: $(BUILT_SOURCES)
	$(MAKE) -C tools
	if test "x$(CHECK_TWISTED_SLEEP)" = x0; then \
		haze_test_sleep= ; \
	else \
		haze_test_sleep=--sleep=$(CHECK_TWISTED_SLEEP); \
	fi; \
	CHECK_TWISTED_UNINSTALLED=1 \
	  G_TEST_SRCDIR=@abs_srcdir@ \
	  G_TEST_BUILDDIR=@abs_builddir@ \
	  CHECK_TWISTED_SLEEP=$$haze_test_sleep \
	  ./run-test.sh "$(TWISTED_TESTS)"

dist_test_data = \
	$(TWISTED_TESTS) \
	constants.py \
	gabbletest.py \
	hazetest.py \
	sasl/saslutil.py \
	servicetest.py \
	ns.py \
	$(NULL)

EXTRA_DIST = \
	$(dist_test_data) \
	run-test.sh.in \
	$(NULL)

haze-twisted-tests.list: Makefile
	$(AM_V_GEN)echo $(TWISTED_TESTS) > $@

built_test_data = \
	haze-twisted-tests.list \
	$(NULL)

built_test_scripts = \
		run-test.sh \
		$(NULL)

BUILT_SOURCES = \
	$(built_test_data) \
	$(built_test_scripts) \
	$(NULL)

testsdir = ${datadir}/telepathy-haze-1-tests
twistedtestsdir = ${testsdir}/twisted

if ENABLE_INSTALLED_TESTS
nobase_dist_twistedtests_DATA = $(dist_test_data)
twistedtests_DATA = $(built_test_data)
twistedtests_SCRIPTS = $(built_test_scripts)
endif

run-test.sh: run-test.sh.in Makefile
	$(AM_V_GEN)sed \
			-e 's![@]hazetestsdir[@]!${testsdir}!' \
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
