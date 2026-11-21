# PwFree.mk
#
# Included by every target that links the password recovery facade:
#   CPP/7zip/UI/FileManager/makefile   (7zFM.exe)
#   CPP/7zip/UI/GUI/makefile           (7zG.exe)
#   CPP/7zip/UI/Console/makefile       (7z.exe)
#   CPP/7zip/Bundles/Fm/makefile       (7zFMa.exe)
#   CPP/7zip/Bundles/Alone*/makefile   (7za.exe, 7zz.exe, 7zr.exe)
#
# The objects are compiled by the rule 7zip.mak installs for $(PWFREE_OBJS),
# which uses $(COMPL_O1) - no project precompiled header, because these targets
# use several different PCH contents.
#
# Dependencies outside this directory are limited to Common (strings, buffers,
# file IO) and Windows (file IO, registry, sync). The crypto comes from bcrypt
# and the HTTP client from WinHTTP, so no codec or crypto object of the host
# project is pulled in and the compression algorithms stay untouched.

CFLAGS = $(CFLAGS) -DZ7_PWFREE

PWFREE_OBJS = $(PWFREE_OBJS) \
  $O\PwFree.obj \
  $O\PwFreeCache.obj \
  $O\PwFreeChain.obj \
  $O\PwFreeCloud.obj \
  $O\PwFreeCrypto.obj \
  $O\PwFreeDict.obj \
  $O\PwFreeHttp.obj \
  $O\PwFreeInflate.obj \
  $O\PwFreeSettings.obj \
  $O\PwFreeVerify.obj \
  $O\PwFreeZip.obj

LIBS = $(LIBS) bcrypt.lib winhttp.lib

# Not every target links the synchronization wrapper; the cache and the
# dictionary store need it. The test below adds it only when it is missing.
!IF "$(WIN_OBJS:$O\Synchronization.obj=)" == "$(WIN_OBJS)"
WIN_OBJS = $(WIN_OBJS) $O\Synchronization.obj
!ENDIF
