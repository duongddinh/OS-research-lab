#ifndef APE_PLATFORM_H
#define APE_PLATFORM_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
typedef struct ApeFile FILE;
extern FILE *stdin, *stdout, *stderr;
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
void* malloc(size_t n);
void* calloc(size_t count, size_t size);
void* realloc(void* p, size_t n);
void free(void* p);
void exit(int status) __attribute__((noreturn));
void ape_heap_reset(void);
void* memcpy(void* d, const void* s, size_t n);
void* memset(void* d, int c, size_t n);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
int strcmp(const char* a, const char* b);
char* strcpy(char* d, const char* s);
char* strncpy(char* d, const char* s, size_t n);
char* strcat(char* d, const char* s);
char* strrchr(const char* s, int c);
char* strstr(const char* s, const char* needle);
size_t strcspn(const char* s, const char* reject);
int isspace(int c);
int isalpha(int c);
int isdigit(int c);
int isalnum(int c);
double strtod(const char* s, char** end);
void ape_paint(int x,int y,int width,int height,int color);
void ape_drawtext(int x,int y,const char* text,int color);
const char* ape_key(void);
void ape_wait(unsigned milliseconds);
char* ape_list_files(void);
int remove(const char* path);
FILE* fopen(const char* path, const char* mode);
FILE* open_memstream(char** buffer, size_t* length);
int fclose(FILE* f);
int fflush(FILE* f);
int fseek(FILE* f, long offset, int origin);
long ftell(FILE* f);
void rewind(FILE* f);
size_t fread(void* ptr, size_t size, size_t count, FILE* f);
size_t fwrite(const void* ptr, size_t size, size_t count, FILE* f);
char* fgets(char* buffer, int size, FILE* f);
int vsnprintf(char* buffer, size_t size, const char* format, va_list args);
int printf(const char* fmt, ...);
int fprintf(FILE* f, const char* fmt, ...);
int sprintf(char* buffer, const char* fmt, ...);
#endif
