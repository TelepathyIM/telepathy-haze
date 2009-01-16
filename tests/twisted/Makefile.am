TWISTED_TESTS = \
	connect/success.py

TESTS =

TESTS_ENVIRONMENT = \
	PYTHONPATH=@abs_top_srcdir@/tests/twisted:@abs_top_builddir@/tests/twisted

check-local: check-twisted

check-twisted:
	sh $(top_srcdir)/tools/with-session-bus.sh \
		--config-file=$(top_builddir)/tests/tmp-session-bus.conf \
		-- $(MAKE) check-TESTS \
			TESTS="$(TWISTED_TESTS)" \
			TESTS_ENVIRONMENT="$(TESTS_ENVIRONMENT) $(TEST_PYTHON)"

EXTRA_DIST = \
	$(TWISTED_TESTS) \
	hazetest.py \
	servicetest.py \
	ns.py

CLEANFILES = *.pyc */*.pyc
