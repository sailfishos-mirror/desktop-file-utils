
INCLUDES=$(DESKTOP_FILE_UTILS_CFLAGS)

noinst_PROGRAMS=run-test

run_test_SOURCES=			\
	run-test.c

run_test_LDADD=$(DESKTOP_FILE_UTILS_LIBS)

TEST_DATA_DIRS=test-data-01

TESTS=all-tests.sh

EXTRA_DIST=all-tests.sh

dist-hook:															       \
	for DIR in $(TEST_DATA_DIRS); do											       \
		echo '-- Disting test data dir '$$DIR;										       \
		mkdir $(distdir)/$$DIR;												       \
		(installfiles=`find $(srcdir)/$$DIR -name "*.desktop" -o -name "*.directory" -o -name ".directory" -o -name "*.menu"`; \
		 for i in $$installfiles; do											       \
			echo '-- Disting '$$i ;											       \
		    	cp $$i $(distdir)/$$DIR;										       \
	  	 done)														       \
	done
