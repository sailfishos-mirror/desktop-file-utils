man_MANS =					\
	desktop-file-validate.1			\
	desktop-file-install.1			\
	update-desktop-database.1

install-exec-hook:
	cd $(DESTDIR)$(mandir)/man1 && \
	rm -f desktop-file-edit.1 && \
	$(LN_S) -f desktop-file-install.1 desktop-file-edit.1

uninstall-hook:
	cd $(DESTDIR)$(mandir)/man1 && \
	rm -f desktop-file-edit.1

EXTRA_DIST = $(man_MANS)

-include $(top_srcdir)/git.mk
