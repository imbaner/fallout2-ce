#include "localcharset.h"

/* The importer wants names as UTF-8, whatever the phone's locale. */
const char* locale_charset(void)
{
    return "UTF-8";
}
