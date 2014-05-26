# This file is Copyright (C) 2014 JimF,
# and is hereby released to the general public under the following terms:
# Redistribution and use in source and binary forms, with or without
# modifications, are permitted.
#
# All tests in this file are supposed to be cross compile compliant
#
AC_DEFUN([JTR_SYSTEMS_SPECIFIC_LOGIC], [
CC_BACKUP=$CC

#############################################################################
# check for using .exe or -ln -s for cygwin/mingw builds only.  Default uses
# symlink.c and exe. cygwin can use --enable-ln-s to override this with ln -s
#############################################################################
case "$host_os" in
  cygwin*)
    AC_MSG_CHECKING([for *2john helper type])
	if test "x$enable_ln_s" != "xno"; then
	  AC_MSG_RESULT([ln -s])
	else
	  AC_SUBST([EXE_EXT], [.exe])
	  AC_MSG_RESULT([.exe (symlink.c)])
	fi
	AC_MSG_CHECKING([for cygwin64])
	if test "x$CPU_BIT_STR" = "x64"; then
	  AC_MSG_RESULT([yes])
	  ax_intel_x32=no
	  JTR_LIST_ADD(EXTRA_AS_FLAGS, [-D__CYGWIN64__ -D__CYGWIN32__])
	  JTR_LIST_ADD(CFLAGS_EX, [-D__CYGWIN64__ -D__CYGWIN32__])
	  CFLAGS_EXTRA="$CFLAGS_EXTRA $CFLAGS_EX"
	else
	  AC_MSG_RESULT([no])
	fi
	;;
  mingw*)
     AC_SUBST([EXE_EXT], [.exe])
	 AC_MSG_CHECKING([for *2john helper type])
	 AC_MSG_RESULT([.exe (symlink.c)]);;
esac

#############################################################################
# Correct behavior for freebsd requires -D__BSD_VISIBLE to be in cflags
#############################################################################
case "$host_os" in
  freebsd*)
    JTR_LIST_ADD(CFLAGS_EX, [-D__BSD_VISIBLE])
    CFLAGS_EXTRA="$CFLAGS_EXTRA $CFLAGS_EX"
	AC_MSG_CHECKING([for FreeBSD])
	AC_MSG_RESULT([yes])
	;;
esac

CC="$CC_BACKUP"
CFLAGS="$CFLAGS_BACKUP"
])
