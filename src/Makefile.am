
INCLUDES=$(DESKTOP_FILE_UTILS_CFLAGS) -DDATADIR="\"$(datadir)\""

## note that TESTS has special meaning (stuff to use in make check)
## so if adding tests not to be run in make check, don't add them to 
## TESTS
if DFU_BUILD_TESTS
TESTS_ENVIRONMENT=
TESTS=dfu-test
else
TESTS=
endif

## we use noinst_PROGRAMS not check_PROGRAMS so that we build 
## even when not doing "make check"
noinst_PROGRAMS=$(TESTS)

bin_PROGRAMS=desktop-file-validate desktop-file-install desktop-menu-tool

desktop_file_validate_SOURCES=			\
	desktop_file.c				\
	desktop_file.h				\
	validate.c				\
	validate.h				\
	validator.c

desktop_file_install_SOURCES=			\
	desktop_file.c				\
	desktop_file.h				\
	validate.c				\
	validate.h				\
	install.c

desktop_menu_tool_SOURCES=			\
	canonicalize.c				\
	canonicalize.h				\
	desktop_file.c				\
	desktop_file.h				\
	gen-compat-tree.c			\
	menu-entries.c				\
	menu-entries.h				\
	menu-layout.c				\
	menu-layout.h				\
	menu-parser.c				\
	menu-parser.h				\
	menu-process.c				\
	menu-process.h				\
	validate.c				\
	validate.h				\
	vfolder-parser.c			\
	vfolder-parser.h			\
	vfolder-query.c				\
	vfolder-query.h

dfu_test_SOURCES=				\
	canonicalize.c				\
	canonicalize.h				\
	menu-entries.c				\
	menu-entries.h				\
	menu-layout.c				\
	menu-layout.h				\
	menu-parser.c				\
	menu-parser.h				\
	dfu-test.c

desktop_file_validate_LDADD=$(DESKTOP_FILE_UTILS_LIBS)
desktop_file_install_LDADD= $(DESKTOP_FILE_UTILS_LIBS)
desktop_menu_tool_LDADD=$(DESKTOP_FILE_UTILS_LIBS)
dfu_test_LDADD=$(DESKTOP_FILE_UTILS_LIBS)
