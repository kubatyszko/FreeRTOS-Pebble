#pragma once
/* rebble_memory.h
 * routines for Allocating memory for system and apps. 
 * App memory gets alocated ont he app's own heap where sys has a global heap
 * RebbleOS
 *
 * Author: Barry Carter <barry.carter@gmail.com>
 */

#include "FreeRTOS.h"
#include <string.h>
#include <stdlib.h>
#include "stdbool.h"

#define malloc rcore_malloc
#define calloc rcore_calloc
#define free rcore_free

void rcore_memory_init(void);
bool rcore_memory_sanity_check_app(size_t size);

size_t xPortGetMinimumEverFreeAppHeapSize( void );
size_t xPortGetFreeAppHeapSize( void );


void *rcore_malloc(size_t size);
void *rcore_calloc(size_t count, size_t size);
void rcore_free(void *mem);

// for app allocator. XXX should have own file

void *pvPortAppMalloc( size_t xWantedSize );
void appHeapInit(size_t xTotalHeapSize, uint8_t *e_app_stack_heap);
