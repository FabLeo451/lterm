EXTRA_DIST = credits.txt lterm.desktop.in lterm.xml \
             preferences.glade profile.glade transfer.glade edit-connection.glade sftp.glade login.glade \
             find.glade credits.glade  preferences.glade.gtk2 profile.glade.gtk2 transfer.glade.gtk2 \
             edit-connection.glade.gtk2 sftp.glade.gtk2 login.glade.gtk2 find.glade.gtk2 credits.glade.gtk2 \
             cluster.glade cluster.glade.gtk2 time.glade time.glade.gtk2 transfer-item.glade transfer-item.glade.gtk2 \
             additionals.xml types.xml PuttyExportTemplate.txt

pkgdatadir = $(datadir)/$(PACKAGE)/data
pkgdata_DATA = credits.txt lterm.desktop.in lterm.xml \
             preferences.glade profile.glade transfer.glade edit-connection.glade sftp.glade login.glade \
             find.glade credits.glade  preferences.glade.gtk2 profile.glade.gtk2 transfer.glade.gtk2 \
             edit-connection.glade.gtk2 sftp.glade.gtk2 login.glade.gtk2 find.glade.gtk2 credits.glade.gtk2 \
             cluster.glade cluster.glade.gtk2 time.glade time.glade.gtk2 transfer-item.glade transfer-item.glade.gtk2 \
             additionals.xml types.xml PuttyExportTemplate.txt

desktopdir = $(datadir)/applications
desktop_in_files = lterm.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

lterm.desktop: lterm.desktop.in
	sed -e 's!IMGDIR!$(datadir)/$(PACKAGE)/img!' < lterm.desktop.in > lterm.desktop

clean-local:
	rm -f lterm.desktop

#xdgdatadir=$(prefix)/share
#mimedir=$(xdgdatadir)/mime
#xmldir=$(mimedir)/packages
#xml_DATA = lterm.xml
#
#install-data-hook:
#	$(UPDATE_MIME_DATABASE) "$(DESTDIR)$(mimedir)"
#
#uninstall-hook:
#	$(UPDATE_MIME_DATABASE) "$(DESTDIR)$(mimedir)"

fdomime_in_files  = lterm.xml

if INSTALL_FDOMIME
fdomimedir = $(fdomimepath)/packages
fdomime_DATA = $(fdomime_in_files:.xml.in=.xml)
endif

install-data-hook:
if INSTALL_FDOMIME
if HAVE_UPDATE_MIME_DATABASE
	$(UPDATE_MIME_DATABASE) $(DESTDIR)$(fdomimepath)
endif
endif


uninstall-hook:
if INSTALL_FDOMIME
if HAVE_UPDATE_MIME_DATABASE
	$(UPDATE_MIME_DATABASE) $(DESTDIR)$(fdomimepath)
endif
endif


