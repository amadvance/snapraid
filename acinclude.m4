dnl @synopsis AC_CHECK_CC_OPT(flag, ifyes, ifno)
dnl 
dnl Shows a message as like "checking wether gcc accepts flag ... no"
dnl and executess ifyes or ifno.

AC_DEFUN([AC_CHECK_CC_OPT],
[
AC_MSG_CHECKING([whether ${CC-cc} accepts $1])
echo 'void f(){}' > conftest.c
if test -z "`${CC-cc} -c $1 conftest.c 2>&1`"; then
  AC_MSG_RESULT([yes])
  $2
else
  AC_MSG_RESULT([no])
  $3
fi
rm -f conftest*
])


