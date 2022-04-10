#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([libdemulti2], [0.2])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign])
LT_INIT([disable-static])
AC_CONFIG_SRCDIR([src/demulti2.h])
AC_CONFIG_HEADERS([config.h])

AC_CANONICAL_HOST
AM_CONDITIONAL([HOST_X86_64], [test "x$host_cpu" == xx86_64])

# include libyakisoba support or not
AC_ARG_ENABLE([yakisoba],
  [AS_HELP_STRING([--disable-yakisoba],
    [do not use libyakisoba @<:@default: yes@:>@])],
  [],
  [enable_yakisoba=yes])
AS_IF([test "x$enable_yakisoba" != xno],
  [AC_DEFINE([CONFIG_YAKISOBA], [1], [Define if you use libyakisoba])])

AC_ARG_WITH([winscard],
  [AS_HELP_STRING([--with-winscard=DIR],
    [specify the location of winscard.h @<:@default: /usr/include/PCSC@:>@])],
  [],
  [with_winscard=/usr/include/PCSC])

AC_CACHE_CHECK([if winscard.h is available], [ac_cv_header_winscard_h],
  [saved_cppflags=$CPPFLAGS
   CPPFLAGS="-I$with_winscard $CPPFLAGS"
   AC_CHECK_HEADER([winscard.h],
     [ac_cv_header_winscard_h=yes], [ac_cv_header_windscard_h=no])
   CPPFLAGS=$saved_cppflags])

AS_IF([test "x$ac_cv_header_winscard_h" != xno],
  [AC_SUBST([PCSCCPPFLAGS], [-I"$with_winscard"])])

# include libsobacas support or not
AC_ARG_ENABLE([sobacas],
  [AS_HELP_STRING([--disable-sobacas],
    [do not use libsobacas @<:@default: yes@:>@])],
  [],
  [enable_sobacas=yes])
AS_IF([test "x$enable_sobacas" != xno -a "x$ac_cv_header_winscard_h" == xyes ],
  [AC_DEFINE([CONFIG_SOBACAS], [1], [Define if you use libsobacas])],
  [enable_sobacas=no])

# include libpcsclite support or not
AC_ARG_ENABLE([pcsc],
  [AS_HELP_STRING([--disable-pcsc],
    [do not use libpcsclite @<:@default: yes@:>@])],
  [],
  [enable_pcsc=yes])
AS_IF([test "x$enable_pcsc" != xno -a "x$ac_cv_header_winscard_h" == xyes ],
  [AC_DEFINE([CONFIG_PCSC], [1], [Define if you use libpcslite(PC/SC)])],
  [enable_pcsc=no])

AM_CONDITIONAL([USE_PCSC],
  [test "x$enable_sobacas" != xno -o "x$enable_pcsc" != xno])

# Checks for programs.
AC_PROG_CC
AM_PROG_AS

# Checks for libraries.
AC_CHECK_LIB([dl], [dlopen], [],
  [AC_MSG_ERROR([libdl is required to build this package.])])

# Checks for header files.
AC_CHECK_HEADERS([inttypes.h stdlib.h string.h unistd.h])

AC_CACHE_CHECK([whether endian.h exists], [ac_cv_header_endian_h],
               [AC_CHECK_HEADERS([endian.h])])
AC_CACHE_CHECK([whether sys/endian.h exists], [ac_cv_header_sys_endian_h],
               [AC_CHECK_HEADERS([sys/endian.h])])
AS_IF([test "x$ac_cv_header_endian_h" == x && "x$ac_cv_header_sys_endian_h" = x],
  [AC_MSG_ERROR([endian.h is required to build this package.])])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_BIGENDIAN
AC_C_INLINE
AC_C_TYPEOF
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_CHECK_FUNCS([malloc])
AC_CHECK_FUNCS([strdup])
AC_CHECK_FUNC([betoh64])

AC_CONFIG_FILES([Makefile
                 src/Makefile])
AC_OUTPUT
