/* libarchive's configuration for its reading part as built here (see
 * CMakeLists.txt): what Android (API 24+) and macOS both have; zlib and
 * liblzma come from third_party, no iconv: names are converted to UTF-8 by
 * libarchive's own code, told the character set is UTF-8 - by
 * android/localcharset.c on Android (nl_langinfo is API 26+), by
 * nl_langinfo of the UTF-8 locale the importer gives its thread elsewhere;
 * old RARs' DOS names are decoded by the importer. */
#ifndef FALLOUT_ARCHIVE_CONFIG_H
#define FALLOUT_ARCHIVE_CONFIG_H

#define HAVE_LIBZ 1
#define HAVE_ZLIB_H 1
#define HAVE_LIBLZMA 1
#define HAVE_LZMA_H 1

#define HAVE_CTYPE_H 1
#define HAVE_DIRENT_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_LOCALE_H 1
#define HAVE_PTHREAD_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_STDARG_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_TIME_H 1
#define HAVE_UNISTD_H 1
#define HAVE_WCHAR_H 1
#define HAVE_WCTYPE_H 1

#define HAVE_DECL_INT32_MAX 1
#define HAVE_DECL_INT32_MIN 1
#define HAVE_DECL_INT64_MAX 1
#define HAVE_DECL_INT64_MIN 1
#define HAVE_DECL_INTMAX_MAX 1
#define HAVE_DECL_INTMAX_MIN 1
#define HAVE_DECL_SIZE_MAX 1
#define HAVE_DECL_SSIZE_MAX 1
#define HAVE_DECL_UINT32_MAX 1
#define HAVE_DECL_UINT64_MAX 1
#define HAVE_DECL_UINTMAX_MAX 1

#define HAVE_ARC4RANDOM_BUF 1
#define HAVE_EILSEQ 1
#define HAVE_FSTAT 1
#define HAVE_GMTIME_R 1
#define HAVE_LOCALTIME_R 1
#define HAVE_LSTAT 1
#define HAVE_MBRTOWC 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMSET 1
#define HAVE_SETLOCALE 1
#define HAVE_STRCHR 1
#define HAVE_STRDUP 1
#define HAVE_STRERROR 1
#define HAVE_STRRCHR 1
#define HAVE_TIMEGM 1
#define HAVE_WCRTOMB 1
#define HAVE_WCSCMP 1
#define HAVE_WCSCPY 1
#define HAVE_WCSLEN 1
#define HAVE_WCTOMB 1
#define HAVE_WMEMCMP 1
#define HAVE_WMEMCPY 1
#define HAVE_WMEMMOVE 1

#define HAVE_INTMAX_T 1
#define HAVE_UINTMAX_T 1
#define HAVE_LONG_LONG_INT 1
#define HAVE_UNSIGNED_LONG_LONG 1
#define HAVE_UNSIGNED_LONG_LONG_INT 1
#define HAVE_WCHAR_T 1
#define SIZEOF_INT 4
#define SIZEOF_WCHAR_T 4

#define STDC_HEADERS 1

#if defined(__ANDROID__)
#define HAVE_LOCALE_CHARSET 1
#define HAVE_LOCALCHARSET_H 1
#else
#define HAVE_LANGINFO_H 1
#define HAVE_NL_LANGINFO 1
#endif

#endif /* FALLOUT_ARCHIVE_CONFIG_H */
