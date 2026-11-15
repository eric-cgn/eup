/* Inside the core: memory that eup_free gives back, and messages */
#ifndef EUP_PRIVATE_H
#define EUP_PRIVATE_H

#include "eup.h"

void *eup_alloc(EupConfig *c, size_t n);
PStr eup_pstr(EupConfig *c, const char *s, int n);
/* fmt with %s (a, then b), %d (a is a decimal number as text), and %%, into out (200 bytes) */
void eup_fmt(char *out, const char *fmt, const char *a, const char *b);

#endif
