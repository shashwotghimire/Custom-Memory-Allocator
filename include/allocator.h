#ifndef CUSTOM_ALLOCATOR_H
#define CUSTOM_ALLOCATOR_H

#include <stddef.h>
#include <stdbool.h>

// memory protection flags

#define MEM_READ   0x1
#define MEM_WRITE  0x2
#define MEM_EXEC   0x4

// ALLOCATOR CONFIG STRUCT

typedef struct {
    size_t initial_heap_size;  //initial size of the managed memory pool 
    size_t page_size;          //size of single page(normally 4kb)
    bool use_guard_pages;      //whether to use guard pages for overflow detection
    int allocation_strategy;  //0== first fit 1== best fit and 2 == worst fit
}allocator_config_t;


// memory region information

typedef struct {
    void* start_addr;       //start address of allocated region
    size_t size;            //size of region in bytes
    int protection;         // protection flags read/write/exec
    bool is_free;           // is the region free ?
} memory_region_t;


// memory stats

typedef struct {
    size_t total_memory;            //total memory allocated
    size_t used_memory;             // currently allocated memory
    size_t free_memory;             // available memory
    size_t overhead;                // memory used by allocater metadata
    size_t peak_usage;              // peak memory use
    size_t total_allocations;       // no. of mem allocns performed
    size_t active_allocations;      //no of active allocations
    double fragmentation_ratio;     // measure of memory fragmentation
} allocator_stats_t;


// initialize memory allocator with the given configs
// @param config Configuration parameters
// @return true if initialization successful else false

bool allocator_init(allocator_config_t config);

/**
 * allocate memory of the specified size
 * @param size Size of memory to allocate in bytes
 * @return Pointer to allocated memory or NULL if allocation failed
 */

void* mem_alloc(size_t size);

/**
 * Allocate memory of the specified size
 * @param size Size of memory to allcate in bytes
 * @return Pointer to allocated memory or NULL if fialed
 * 
 */

void* mem_alloc_signed(size_t,size_t allignment);

/**
 * free previously allocated memory 
 * @param size Size of memory to allocate in bytes
 * @return Pointer to allocated memory or NULL if allocation failed
 */

void mem_free(void* ptr);
/**
 * reallocate memory to a new size
 * @param size Size of memory to allocate in bytes
 * @return Pointer to allocated memory or NULL if allocation failed
 */

void* mem_realloc(void* ptr, size_t size);

/**
 * Set protection flags for a memory region
 * @param ptr Start of memory region
 * @param size Size of memory region
 * @param protection Protection flags to set
 * @return true if successful, false otherwise
 */
bool mem_protect(void* ptr, size_t size, int protection);

/**
 * Get statistics about memory usage
 * @return Statistics struct
 */
allocator_stats_t allocator_get_stats(void);

/**
 * Print detailed memory map for debugging
 */
void allocator_print_memory_map(void);

/**
 * Clean up and release all resources used by the allocator
 */
void allocator_cleanup(void);

#endif /* CUSTOM_ALLOCATOR_H */