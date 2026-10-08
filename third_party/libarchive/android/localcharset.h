/* libarchive's character set on Android (archive_config.h): nl_langinfo
 * comes with API 26, the app runs from 24. */
#ifndef FALLOUT_LOCALCHARSET_H
#define FALLOUT_LOCALCHARSET_H

const char* locale_charset(void);

#endif /* FALLOUT_LOCALCHARSET_H */
