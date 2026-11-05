#include "../util.h"
size_t strlen(const char *s) { size_t n=0; while (s[n]) n++; return n; }
int strcmp(const char *a,const char *b) { while (*a && *a==*b) { a++; b++; } return (unsigned char)*a-(unsigned char)*b; }
char *strcpy(char *d,const char *s) { char *start=d; while ((*d++=*s++)) {} return start; }
char *strncpy(char *d,const char *s,size_t n) {
    size_t i=0; for (;i<n && s[i];i++) d[i]=s[i]; for (;i<n;i++) d[i]=0; return d;
}
void *memset(void *p,int c,size_t n) { unsigned char *d=p; while (n--) *d++=(unsigned char)c; return p; }
void *memcpy(void *p,const void *q,size_t n) { unsigned char *d=p; const unsigned char *s=q; while (n--) *d++=*s++; return p; }
