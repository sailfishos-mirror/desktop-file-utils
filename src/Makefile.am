
INCLUDES=$(DESKTOP_FILE_UTILS_CFLAGS) -DDATADIR="\"$(datadir)\""

bin_PROGRAMS=desktop-file-validate desktop-file-install desktop-menu-gen-compat-dir

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

desktop_menu_gen_compat_dir_SOURCES=		\
	desktop_file.c				\
	desktop_file.h				\
	gen-compat-tree.c			\
	validate.c				\
	validate.h				\
	vfolder-parser.c			\
	vfolder-parser.h			\
	vfolder-query.c				\
	vfolder-query.h

desktop_file_validate_LDADD=$(DESKTOP_FILE_UTILS_LIBS)
desktop_file_install_LDADD= $(DESKTOP_FILE_UTILS_LIBS)
desktop_menu_gen_compat_dir_LDADD=$(DESKTOP_FILE_UTILS_LIBS)