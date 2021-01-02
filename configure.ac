dnl Process this file with autoconf to produce a configure script.

AC_INIT(ghostess, 20210101, sean-at-smbolton-dot-com)

AC_CONFIG_SRCDIR(src/ghostess.c)
AM_INIT_AUTOMAKE
AM_CONFIG_HEADER(config.h)

AC_PROG_CC

dnl AC_ENABLE_STATIC(no)
dnl AC_ENABLE_SHARED(yes)
AC_LIBTOOL_DLOPEN
AC_PROG_LIBTOOL

AC_C_BIGENDIAN

dnl Check for GTK+
AM_PATH_GTK_2_0(2.8.0)

dnl OS specific checks
case "${host_os}" in
darwin*)
  darwin=yes
  AC_DEFINE(MIDI_COREMIDI, 1, [Define for CoreMIDI support on Mac OS X])
  PKG_CHECK_MODULES(ALSA, libdssialsacompat)
  ;;
*)
  darwin=no
  PKG_CHECK_MODULES(ALSA, alsa >= 0.9.0)
  ;;
esac
AM_CONDITIONAL(DARWIN, test x$darwin = xyes)

dnl Check for LADSPA
AC_CHECK_HEADERS(ladspa.h)

dnl Require DSSI and liblo
PKG_CHECK_MODULES(MODULE, dssi >= 0.9 liblo >= 0.12)

dnl Check for JACK: need jack 0.99.14+ for jack_client_open()
PKG_CHECK_MODULES(JACK, jack >= 0.99.14)
AC_SUBST(JACK_CFLAGS)
AC_SUBST(JACK_LIBS)

dnl Check for JACK MIDI support
AC_ARG_WITH(jackmidi,AC_HELP_STRING([--with-jackmidi], [default=yes]),
	[ if test $withval = "yes";then with_jack_midi=yes;
		else with_jack_midi=no; fi ])
if test x${with_jack_midi} = 'xyes'; then
AC_CHECK_LIB(jack, jack_midi_get_event_count,
             [with_jack_midi=yes
              AC_DEFINE(MIDI_JACK, 1, [Define for JACK MIDI support])],
             with_jack_midi=no, $JACK_LIBS)
else if test x${darwin} = 'xno'; then
	AC_DEFINE(MIDI_ALSA, 1, [Define for ALSA MIDI support on Linux])
fi fi
AM_CONDITIONAL(MIDI_JACK, test x$with_jack_midi = xyes)

saved_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS $JACK_CFLAGS"
saved_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $JACK_CFLAGS"
saved_LDFLAGS="$LDFLAGS"
LDFLAGS="$LDFLAGS $JACK_LIBS"
AC_CHECK_HEADER( jack/session.h, have_jack_session="yes", have_jack_session="no" )
if test x${have_jack_session} = 'xyes'; then
	AC_DEFINE(JACK_SESSION, 1, [Define for JACK session support])
fi
CFLAGS="$saved_CFLAGS"
CPPFLAGS="$saved_CPPFLAGS"
LDFLAGS="$saved_LDFLAGS"

dnl Use lotsa flags if we have gcc.
CFLAGS="$CFLAGS $ALSA_CFLAGS $MODULE_CFLAGS"
LDFLAGS="$LDFLAGS $ALSA_LIBS $MODULE_LIBS"
changequote(,)dnl
if test "x$GCC" = "xyes"; then
  case " $CFLAGS " in
  *[\ \	]-Wall[\ \	]*) ;;
  *) CFLAGS="$CFLAGS -Wall" ;;
  esac
  case " $CFLAGS " in
  *[\ \	]-O[1-9][\ \	]*) ;;
  *) CFLAGS="$CFLAGS -O2 -s -fomit-frame-pointer -funroll-all-loops -finline-functions -ffast-math -Winline" ;;
  esac
  # add '-finline-limit=...' only for GCC 3.x:
  #case `$CC -dumpversion 2>/dev/null` in
  #3.*) CFLAGS="$CFLAGS -finline-limit=5000" ;;
  #esac
fi
changequote([,])dnl

AC_OUTPUT([
Makefile
src/Makefile
])

