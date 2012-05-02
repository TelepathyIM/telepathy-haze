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
	text/destroy.py \
	text/ensure.py \
	text/initiate-requestotron.py \
	text/initiate.py \
	text/respawn.py \
	text/test-text-delayed.py \
	text/test-text-no-body.py \
	text/test-text.py


TESTS =

TESTS_ENVIRONMENT = \
	PYTHONPATH=@abs_top_srcdir@/tests/twisted:@abs_top_builddir@/tests/twisted

check-local: check-twisted

check-twisted:
	rm -f ../haze-testing.log
	sh $(top_srcdir)/tools/with-session-bus.sh \
		--config-file=$(top_builddir)/tests/tmp-session-bus.conf \
		-- $(MAKE) check-TESTS \
			TESTS="$(TWISTED_TESTS)" \
			TESTS_ENVIRONMENT="$(TESTS_ENVIRONMENT) $(TEST_PYTHON)"

EXTRA_DIST = \
	$(TWISTED_TESTS) \
	constants.py \
	hazetest.py \
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

BUILT_SOURCES = config.py

CLEANFILES = *.pyc */*.pyc config.py
