#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT(lterm, 1.5.1)
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/preferences.c])
AM_CONFIG_HEADER(src/config.h)

# Checks for programs.
AC_PROG_CC

AC_PATH_PROG([XMLCATALOG], [xmlcatalog], [no])
if test "x$XMLCATALOG" = "xno"; then
        AC_MSG_WARN([Install the xmlcatalog application, which is delivered with the GNOME xml-library.])
fi

AC_ARG_ENABLE(
        [xml-catalog-update],
        AC_HELP_STRING(
                [--enable-xml-catalog-update],
                [
                 enable (or disable) the running of xmlcatalog after installation -
                 some distributions like Debian use their own system for registering
                 catalogs, so be careful here and also check the `--with-xml-catalog'
                 option @<:@default=yes@:>@
                ]
        ),
)

AC_ARG_ENABLE(
        [update-databases],
        AC_HELP_STRING(
                [--enable-update-databases],
                [
                 enable (or disable) the running of update-desktop-database/update-mime-database
                 after installation @<:@default=yes@:>@
                ]
        )
)

# Debug
AC_ARG_ENABLE([debug], 
              AC_HELP_STRING([--enable-debug], [build with debugging code]),
              debug="1", debug="0")

if test $debug = 1 ; then 
  CFLAGS="$CFLAGS -DDEBUG"
fi

AC_ARG_WITH([gtk2], [AS_HELP_STRING([--with-gtk2],  [forces linking with gtk+ 2.0])], [usegtk2="1"], [usegtk2="0"])

AC_ARG_VAR(
        [UPDATE_DESKTOP_DATABASE],
        [The `update-desktop-database' program. Use it to define or override its location.]
)
AC_ARG_VAR(
        [UPDATE_MIME_DATABASE],
        [The `update-mime-database' program. Use it to define or override its location.]
)
if test "x$enable_update_databases" != "xno" ; then
        AC_PATH_PROG([UPDATE_DESKTOP_DATABASE], [update-desktop-database])
        AC_PATH_PROG([UPDATE_MIME_DATABASE], [update-mime-database])
fi

AM_CONDITIONAL([HAVE_UPDATE_DESKTOP_DATABASE], [test -n "$UPDATE_DESKTOP_DATABASE"])
AM_CONDITIONAL([HAVE_UPDATE_MIME_DATABASE], [test -n "$UPDATE_MIME_DATABASE"])

AC_ARG_WITH(
        [freedesktop_org-mime], 
        AC_HELP_STRING(
                [--with-freedesktop_org-mime=@<:@DIR@:>@],
                [install freedesktop.org mime support files in directory DIR (if supplied) or suppress installation @<:@default=${datadir}/mime@:>@]
        ),
        [fdomimepath="$withval"],
        [fdomimepath="${datadir}/mime"]
)

AC_SUBST([XML_CATALOG_FILE])
AM_CONDITIONAL([REGISTER_XML_CATALOG], [test "x$enable_xml_catalog_update" != "xno" -a -n "$XML_CATALOG_FILE" ])
AC_SUBST([fdomimepath])
AM_CONDITIONAL(INSTALL_FDOMIME, [test "x$fdomimepath" != "xno"])

# Checks for gtk libraries.

printf %s "checking for gtk libs... "

foundgtk=0

if [ test ${usegtk2} = 0 ]; then
  if pkg-config --exists gtk+-3.0; then
    LIBS="$LIBS `pkg-config --libs gtk+-3.0`"
    CFLAGS="$CFLAGS `pkg-config --cflags gtk+-3.0`"
    echo "found gtk 3.0"
    foundgtk=3
  fi
fi

if [ test ${foundgtk} = 0 ]; then
  if pkg-config --exists gtk+-2.0; then
    LIBS="$LIBS `pkg-config --libs gtk+-2.0`"
    CFLAGS="$CFLAGS `pkg-config --cflags gtk+-2.0`"
    echo "found gtk 2.0"
    foundgtk=2
  fi
fi

if [ test ${foundgtk} = 0 ]; then
  echo "no"
  AC_MSG_ERROR([libgtk3.0-dev and libgtk2.0-dev missing, please install at least one])
fi

printf %s "checking for vte emulator widget... "

#foundvte=0
VTEVERS=0

if [ test ${foundgtk} = 3 ]; then
#  if pkg-config --exists vte-2.90; then
#    LIBS="$LIBS `pkg-config --libs vte-2.90`"
#    CFLAGS="$CFLAGS `pkg-config --cflags vte-2.90`"
#    echo "found vte-2.90"
#    foundvte=3
#  fi

  if pkg-config --exists vte-3; then
    VTEVERS=3
  else
    if pkg-config --exists vte-2.91; then
      VTEVERS=2.91
    else
      if pkg-config --exists vte-2.90; then
        VTEVERS=2.90
      fi
    fi
  fi

  if [ test ${VTEVERS} != 0 ]; then
    LIBS="$LIBS `pkg-config --libs vte-${VTEVERS}`"
    CFLAGS="$CFLAGS `pkg-config --cflags vte-${VTEVERS}`"
    echo "found vte-${VTEVERS}"
    #foundvte=3
  fi

else
  if pkg-config --exists vte; then
    LIBS="$LIBS `pkg-config --libs vte`"
    CFLAGS="$CFLAGS `pkg-config --cflags vte`"
    echo "found vte"
    VTEVERS=1
  fi
fi

if [ test ${VTEVERS} = 0 ]; then
  echo "no"
  AC_MSG_ERROR([VTE Terminal widget development files missing, please install])
fi

# Add encryption library
#LIBS="$LIBS -lcrypt"

#AC_CHECK_LIB(ssl, DES_cfb64_encrypt, [], [
#    echo "You need libssl-dev to be installed."
#    exit -1
#    ])

printf %s "checking for openssl lib... "

if pkg-config --exists openssl ; then
        LIBS="$LIBS `pkg-config --libs openssl`"
        CFLAGS="$CFLAGS `pkg-config --cflags openssl`"
        echo "yes"
else
        echo "no"
        AC_MSG_ERROR([libssl development files missing, please install])
fi

printf %s "checking for libssh... "

if pkg-config --exists libssh ; then
        LIBS="$LIBS `pkg-config --libs libssh`"
        CFLAGS="$CFLAGS `pkg-config --cflags libssh`"
        echo "yes"
else
        echo "no"
        AC_MSG_ERROR([libssh development files missing, please install])
#  AC_CHECK_FILE(/usr/include/libssh/libssh.h,
#		[LIBS="$LIBS -lssh"],
#		[AC_MSG_ERROR([libssh development files missing, please install])])
fi

LIBS="$LIBS -lssh_threads -lX11"

### Check to see if GDK uses the quartz backend and if we can use 
### MacOSX integration

# this is needed by gtk2
#AM_CONDITIONAL([GDK_TARGET_QUARTZ], [test x$_gdk_tgt = xquartz])

if [ test ${foundgtk} = 3 ]; then
  GTK_CHECK_BACKEND([quartz], [3.0], [osx=yes], [osx=no])
  if test "$osx" = "yes"; then
          PKG_CHECK_MODULES(GTK_MAC, gtk-mac-integration)
          AC_SUBST(GTK_MAC_LIBS)
          AC_SUBST(GTK_MAC_CFLAGS)
          LIBS="$LIBS -L/opt/X11/lib"
	  AC_MSG_RESULT([yes])
  else
	  AC_MSG_RESULT([no])
  fi
else
  _gdk_tgt=`pkg-config --variable=target gdk-2.0`
  if test "x$_gdk_tgt" = xquartz; then 
     PKG_CHECK_MODULES(GTK_MAC, gtk-mac-integration)
     AC_SUBST(GTK_MAC_LIBS)
     AC_SUBST(GTK_MAC_CFLAGS)
     LIBS="$LIBS -L/opt/X11/lib"
  else
	  AC_MSG_RESULT([no])
  fi
fi

CFLAGS="$CFLAGS $GTK_MAC_CFLAGS"
LIBS="$LIBS $GTK_MAC_LIBS"

#echo $CFLAGS

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h libintl.h locale.h stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MKTIME
AC_CHECK_FUNCS([memset mkdir regcomp setlocale strcasecmp strchr strncasecmp strrchr])

AC_CONFIG_FILES([Makefile src/Makefile img/Makefile img/types/Makefile data/Makefile])
AC_OUTPUT
