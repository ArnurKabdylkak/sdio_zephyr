/**
 * Bare-metal Runtime Support
 * Minimal libc replacement for freestanding environment
 */

#ifndef BAREMETAL_H
#define BAREMETAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*============================================================================
 * String Functions (implemented in libc.c)
 *============================================================================*/

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
int strcmp(const char *s1, const char *s2);

/*============================================================================
 * NULL definition
 *============================================================================*/

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif /* BAREMETAL_H */
