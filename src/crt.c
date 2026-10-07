// The two C runtime functions the installer needs without a C runtime (miniz's inflate and
// compiler-generated structure copies).
#include <string.h>

#pragma function(memset, memcpy)

void *memset(void *d, int c, size_t n)
{
    volatile unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    volatile unsigned char *p = d; const unsigned char *q = s;
    while (n--) *p++ = *q++;
    return d;
}
