
INCLUDES=$(DESKTOP_FILE_UTILS_CFLAGS)

bin_PROGRAMS=desktop-file-validate desktop-file-install

desktop_file_validate_SOURCES=			\
	desktop_file.c				\
	desktop_file.h				\
	validate.c

desktop_file_install_SOURCES=			\
	desktop_file.c				\
	desktop_file.h				\
	install.c

desktop_file_validate_LDADD=$(DESKTOP_FILE_UTILS_LIBS)
desktop_file_install_LDADD= $(DESKTOP_FILE_UTILS_LIBS)