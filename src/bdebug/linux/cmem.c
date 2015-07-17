// http://www.apache.org/licenses/LICENSE-2.0
// Copyright 2014 Perttu Ahola <celeron55@gmail.com>
#define _GNU_SOURCE // RTLD_NEXT
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef __GNUC__
	#include <malloc.h> // malloc_usable_size()
#endif
#include <stdio.h>
#include <dlfcn.h>
//#include <unistd.h> // write() for debugging

// This isn't needed for Buildat but let's do it for the sake of it
static const int ENABLE_MEMORY_CLEARING = 1;

static const char FREED_MEMORY_PATTERN = 0x2e;
static const char MALLOC_MEMORY_PATTERN = 0x2d;
static const char REALLOC_MEMORY_PATTERN = 0x4d;

static const char PREINIT_FREED_MEMORY_PATTERN = 0x1e;
static const char PREINIT_REALLOC_MEMORY_PATTERN = 0x1d;

#ifndef __GNUC__
	#warning "GNU extensions unavailable - special C memory allocators disabled."
#else

static int preinit_owns(void *p);
static void* preinit_malloc(size_t size);
static void preinit_free(void *p);
static void* preinit_calloc(size_t, size_t);
static void* preinit_realloc(void *p, size_t size);

static void* (*r_malloc)(size_t) = NULL;
static void  (*r_free)(void*) = NULL;
static void* (*r_calloc)(size_t, size_t) = NULL;
static void* (*r_realloc)(void*, size_t) = NULL;

void cmem_libc_enable()
{
	//fprintf(stderr, "bdebug/cmem: cmem_libc_enable()\n");
	r_malloc  = dlsym(RTLD_NEXT, "malloc");
	r_free    = dlsym(RTLD_NEXT, "free");
	r_calloc  = dlsym(RTLD_NEXT, "calloc");
	r_realloc = dlsym(RTLD_NEXT, "realloc");
}

void cmem_libc_disable()
{
	//fprintf(stderr, "bdebug/cmem: cmem_libc_disable()\n");
	r_malloc  = dlsym(RTLD_NEXT, "malloc");
	r_free    = dlsym(RTLD_NEXT, "free");
	r_calloc  = dlsym(RTLD_NEXT, "calloc");
	r_realloc = dlsym(RTLD_NEXT, "realloc");
}

// These are preferred by the linker (versus the standard library) when linking
// this program.

void* malloc(size_t size)
{
	if(!r_malloc)
		return preinit_malloc(size);
	void *p = r_malloc(size);
	if(p == NULL)
		return NULL;
	if(ENABLE_MEMORY_CLEARING){
		memset((char*)p, MALLOC_MEMORY_PATTERN, size);
	}
	return p;
}

void free(void *p)
{
	if(p == NULL)
		return;
	if(preinit_owns(p))
		return preinit_free(p);
	if(!r_free)
		abort();
	if(ENABLE_MEMORY_CLEARING){
		size_t s = malloc_usable_size(p);
		memset((char*)p, FREED_MEMORY_PATTERN, s);
	}
	return r_free(p);
}

void* calloc(size_t nmemb, size_t size)
{
	if(!r_calloc)
		return preinit_calloc(nmemb, size);
	void *p = r_calloc(nmemb, size);
	if(p == NULL)
		return NULL;
	// The standard calloc sets this to 0, as required by the standard.
	return p;
}

void* realloc(void *p, size_t size)
{
	if(preinit_owns(p))
		return preinit_realloc(p, size);
	if(!r_realloc)
		abort();
	size_t old_size = malloc_usable_size(p);
	void *p2 = r_realloc(p, size);
	if(ENABLE_MEMORY_CLEARING){
		if(size > old_size)
			memset(p2 + old_size, REALLOC_MEMORY_PATTERN, size - old_size);
	}
	return p2;
}

// System-wide memcpy can leak stuff (?)
/*void* memcpy(void *dest, const void *src, size_t n)
{
	size_t i;
	for(i=0; i<n; i++)
		*(char*)(dest++) = *(char*)(src++);
	return dest;
}*/

/* Preinit pool */

// Allocates memory until cmem_libc_enable() is called

#define POOL_SIZE 100000 // 100kB
static char preinit_pool[POOL_SIZE] = {0};
static size_t preinit_pool_i = 0;
#define POINTERS_SIZE (POOL_SIZE / 20)
static void* preinit_pointers[POINTERS_SIZE]; // Pointers given out
static size_t preinit_pointer_sizes[POINTERS_SIZE] = {SIZE_MAX};
static size_t preinit_pointer_i = 0;

static int preinit_owns(void *p)
{
	return (p >= (void*)&preinit_pool[0] &&
			p < (void*)&preinit_pool[POOL_SIZE]);
}
static void* preinit_malloc(size_t size)
{
	if(preinit_pool_i + size > POOL_SIZE){
		fprintf(stderr, "bdebug/cmem: Ran out of preinit memory pool\n");
		abort();
	}
	if(preinit_pointer_i + 1 >= POINTERS_SIZE){
		fprintf(stderr, "bdebug/cmem: Ran out of preinit memory pointers\n");
		abort();
	}
	size_t pool_i = preinit_pool_i;
	preinit_pool_i += size;
	size_t pointer_i = preinit_pointer_i++;
	void *p = &preinit_pool[pool_i];
	preinit_pointers[pointer_i] = p;
	preinit_pointer_sizes[pointer_i] = size;
	return p;
}
static void preinit_free(void *p)
{
	if(p == NULL)
		return;
	size_t pointer_i = SIZE_MAX;
	size_t i;
	for(i=0; i<preinit_pointer_i; i++){
		if(preinit_pointers[i] == p){
			pointer_i = i;
			break;
		}
	}
	if(pointer_i == SIZE_MAX){
		abort(); // Not found
	}
	size_t size = preinit_pointer_sizes[pointer_i];
	memset(p, PREINIT_FREED_MEMORY_PATTERN, size);
	preinit_pointers[pointer_i] = NULL;
}
static void* preinit_calloc(size_t nmemb, size_t size)
{
	void *p = preinit_malloc(nmemb * size);
	memset(p, 0, nmemb * size); // As required by standard
	return p;
}
static void* preinit_realloc(void *p, size_t size)
{
	size_t pointer_i = SIZE_MAX;
	size_t i;
	for(i=0; i<preinit_pointer_i; i++){
		if(preinit_pointers[i] == p){
			pointer_i = i;
			break;
		}
	}
	if(pointer_i == SIZE_MAX){
		abort(); // Not found
	}
	size_t old_size = preinit_pointer_sizes[pointer_i];
	size_t copy_size = old_size < size ? old_size : size;
	void *p2 = malloc(size); // Realloc from OS if possible
	memcpy(p2, p, copy_size);
	if(size > old_size)
		memset(p2 + old_size, PREINIT_REALLOC_MEMORY_PATTERN, size - old_size);
	return p2;
}

#endif

