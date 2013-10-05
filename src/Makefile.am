bin_PROGRAMS =					\
	desktop-file-validate			\
	desktop-file-install			\
	update-desktop-database

AM_CPPFLAGS =					\
	$(DESKTOP_FILE_UTILS_CFLAGS)		\
	-DG_LOG_DOMAIN=\"desktop-file-utils\"	\
	-DDATADIR="\"$(datadir)\"" 		\
	-D_FILE_OFFSET_BITS=64			\
	-D_LARGEFILE64_SOURCE

desktop_file_validate_SOURCES =			\
	keyfileutils.c				\
	keyfileutils.h				\
	mimeutils.c				\
	mimeutils.h				\
	validate.c				\
	validate.h				\
	validator.c

desktop_file_install_SOURCES =			\
	keyfileutils.c				\
	keyfileutils.h				\
	mimeutils.c				\
	mimeutils.h				\
	validate.c				\
	validate.h				\
	install.c

update_desktop_database_SOURCES =		\
	dfi-builder.c				\
	dfi-builder.h				\
	dfi-id-list.c				\
	dfi-id-list.h				\
	dfi-keyfile.c				\
	dfi-keyfile.h				\
	dfi-string-list.c			\
	dfi-string-list.h			\
	dfi-string-table.c			\
	dfi-string-table.h			\
	dfi-text-index.c			\
	dfi-text-index.h			\
	mimeutils.c				\
	mimeutils.h				\
	mime-cache.c				\
	mime-cache.h				\
	update-desktop-database.c

desktop_file_validate_LDADD = $(DESKTOP_FILE_UTILS_LIBS)
desktop_file_install_LDADD = $(DESKTOP_FILE_UTILS_LIBS)
update_desktop_database_LDADD = $(DESKTOP_FILE_UTILS_LIBS)

install-exec-hook: desktop-file-install
	cd $(DESTDIR)$(bindir) && \
	rm -f desktop-file-edit && \
	$(LN_S) -f desktop-file-install desktop-file-edit

uninstall-hook:
	cd $(DESTDIR)$(bindir) && \
	rm -f desktop-file-edit

-include $(top_srcdir)/git.mk
