#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> // For size_t

#define MAX_FILE_SIZE 16384
#define MAX_FILES 16
#define MAX_FILENAME_LEN 32
void    read_line(char *buf, int max);
int     strcmp(const char *a, const char *b);
int32_t atoi(const char *s);
void    itoa(int32_t n, char *buf);
size_t  strlen(const char *s);
char* strcpy(char *dst, const char *src);
char* strncpy(char *dst, const char *src, size_t n);
void* memset(void *s, int c, size_t n);
int memcmp(const void* a, const void* b, size_t n);
void* memcpy(void *dest, const void *src, size_t n);


void sys_write(const char *s);
char sys_getc(void);
char sys_pollc(void);
void sys_yield(void);
void sys_exit_task(void);
void sys_clear_screen(void); 
int sys_list_files(char* out_buffer, int buffer_len);

int sys_read_file(const char* filename, char* data_buffer, int data_buffer_len);

int sys_write_file(const char* filename, const char* data, int data_len);

int sys_delete_file(const char* filename);


#endif 
