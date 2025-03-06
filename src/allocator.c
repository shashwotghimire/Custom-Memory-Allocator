#include "../include/allocator.h"
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <stdint.h>  // Add this include for uintptr_t and SIZE_MAX

/* Internal structures */

typedef struct memory_block {
    size_t size;                  /* Size of this block in bytes (including header) */
    bool is_free;                 /* Whether this block is free */
    int protection;               /* Memory protection flags */
    struct memory_block* next;    /* Next block in list */
    struct memory_block* prev;    /* Previous block in list */
    /* Alignment padding */
    char padding[8];              /* Ensure the header is aligned */
    /* User data follows immediately after this header */
} memory_block_t;

/* Global allocator state */
static struct {
    void* heap_start;             /* Start of our heap */
    size_t heap_size;             /* Current size of heap */
    size_t page_size;             /* Page size of the system */
    memory_block_t* free_list;    /* List of free blocks */
    memory_block_t* used_list;    /* List of used blocks */
    pthread_mutex_t mutex;        /* Mutex for thread safety */
    bool initialized;             /* Whether allocator is initialized */
    allocator_config_t config;    /* Configuration parameters */
    allocator_stats_t stats;      /* Memory usage statistics */
} allocator_state = {0};

/* Helper Functions */

/* Calculate required space including header */
static size_t calculate_total_size(size_t requested_size) {
    return requested_size + sizeof(memory_block_t);
}

/* Round up to the nearest multiple of page size */
static size_t round_up_to_page_size(size_t size) {
    return ((size + allocator_state.page_size - 1) / 
            allocator_state.page_size) * allocator_state.page_size;
}

/* Find a free block using the selected strategy */
static memory_block_t* find_free_block(size_t size) {
    memory_block_t* current = allocator_state.free_list;
    memory_block_t* best_fit = NULL;
    size_t smallest_size_diff = SIZE_MAX;
    
    switch (allocator_state.config.allocation_strategy) {
        case 0: /* First fit */
            while (current != NULL) {
                if (current->is_free && current->size >= size) {
                    return current;
                }
                current = current->next;
            }
            break;
            
        case 1: /* Best fit */
            while (current != NULL) {
                if (current->is_free && current->size >= size) {
                    size_t size_diff = current->size - size;
                    if (size_diff < smallest_size_diff) {
                        smallest_size_diff = size_diff;
                        best_fit = current;
                    }
                }
                current = current->next;
            }
            return best_fit;
            
        case 2: /* Worst fit */
            size_t largest_size_diff = 0;
            while (current != NULL) {
                if (current->is_free && current->size >= size) {
                    size_t size_diff = current->size - size;
                    if (size_diff > largest_size_diff) {
                        largest_size_diff = size_diff;
                        best_fit = current;
                    }
                }
                current = current->next;
            }
            return best_fit;
            
        default:
            /* Default to first fit */
            while (current != NULL) {
                if (current->is_free && current->size >= size) {
                    return current;
                }
                current = current->next;
            }
            break;
    }
    
    return NULL;
}

/* Split a block if it's too large */
static void split_block(memory_block_t* block, size_t size) {
    /* Only split if the remaining space can fit a new block plus some data */
    size_t min_block_size = sizeof(memory_block_t) + 16;
    
    if (block->size >= size + min_block_size) {
        /* Create a new block after the allocated portion */
        memory_block_t* new_block = (memory_block_t*)((char*)block + size);
        
        /* Set up the new block */
        new_block->size = block->size - size;
        new_block->is_free = true;
        new_block->protection = block->protection;
        new_block->next = block->next;
        new_block->prev = block;
        
        /* Update the original block */
        block->size = size;
        block->next = new_block;
        
        /* Update the next block's prev pointer if it exists */
        if (new_block->next != NULL) {
            new_block->next->prev = new_block;
        }
        
        /* Add the new block to the free list */
        if (allocator_state.free_list == NULL) {
            allocator_state.free_list = new_block;
        }
    }
}

/* Try to merge adjacent free blocks */
static void merge_free_blocks(void) {
    memory_block_t* current = allocator_state.free_list;
    
    while (current != NULL && current->next != NULL) {
        if (current->is_free && current->next->is_free) {
            /* This block and the next are both free - merge them */
            memory_block_t* next_block = current->next;
            
            /* Combine the sizes */
            current->size += next_block->size;
            
            /* Skip the next block in the linked list */
            current->next = next_block->next;
            
            /* Update the next block's prev pointer if it exists */
            if (current->next != NULL) {
                current->next->prev = current;
            }
        } else {
            /* Move to the next block */
            current = current->next;
        }
    }
}

/* Extend the heap to create more space */
static memory_block_t* extend_heap(size_t size) {
    size_t aligned_size = round_up_to_page_size(size);
    
    /* Use mmap to allocate more memory */
    void* new_mem = mmap(NULL, aligned_size, 
                         PROT_READ | PROT_WRITE, 
                         MAP_PRIVATE | MAP_ANONYMOUS, 
                         -1, 0);
    
    if (new_mem == MAP_FAILED) {
        return NULL;
    }
    
    /* Create a new block at the start of this memory */
    memory_block_t* new_block = (memory_block_t*)new_mem;
    new_block->size = aligned_size;
    new_block->is_free = true;
    new_block->protection = MEM_READ | MEM_WRITE;
    new_block->next = NULL;
    new_block->prev = NULL;
    
    /* Add to the free list */
    if (allocator_state.free_list == NULL) {
        allocator_state.free_list = new_block;
    } else {
        /* Find the end of the free list */
        memory_block_t* current = allocator_state.free_list;
        while (current->next != NULL) {
            current = current->next;
        }
        
        /* Link the new block */
        current->next = new_block;
        new_block->prev = current;
    }
    
    /* Update allocator state */
    allocator_state.heap_size += aligned_size;
    allocator_state.stats.total_memory += aligned_size;
    allocator_state.stats.free_memory += aligned_size;
    
    return new_block;
}

/* Move a block from free list to used list */
static void mark_block_used(memory_block_t* block) {
    /* Remove from free list if present */
    if (allocator_state.free_list == block) {
        allocator_state.free_list = block->next;
    } else {
        memory_block_t* current = allocator_state.free_list;
        while (current != NULL && current->next != block) {
            current = current->next;
        }
        
        if (current != NULL) {
            current->next = block->next;
            if (block->next != NULL) {
                block->next->prev = current;
            }
        }
    }
    
    /* Add to used list */
    block->next = allocator_state.used_list;
    block->prev = NULL;
    if (allocator_state.used_list != NULL) {
        allocator_state.used_list->prev = block;
    }
    allocator_state.used_list = block;
    
    /* Mark as used */
    block->is_free = false;
    
    /* Update statistics */
    allocator_state.stats.used_memory += block->size;
    allocator_state.stats.free_memory -= block->size;
    allocator_state.stats.active_allocations++;
    allocator_state.stats.total_allocations++;
    
    if (allocator_state.stats.used_memory > allocator_state.stats.peak_usage) {
        allocator_state.stats.peak_usage = allocator_state.stats.used_memory;
    }
}

/* Move a block from used list to free list */
static void mark_block_free(memory_block_t* block) {
    /* Remove from used list if present */
    if (allocator_state.used_list == block) {
        allocator_state.used_list = block->next;
    } else {
        memory_block_t* current = allocator_state.used_list;
        while (current != NULL && current->next != block) {
            current = current->next;
        }
        
        if (current != NULL) {
            current->next = block->next;
            if (block->next != NULL) {
                block->next->prev = current;
            }
        }
    }
    
    /* Add to free list */
    block->next = allocator_state.free_list;
    block->prev = NULL;
    if (allocator_state.free_list != NULL) {
        allocator_state.free_list->prev = block;
    }
    allocator_state.free_list = block;
    
    /* Mark as free */
    block->is_free = true;
    
    /* Update statistics */
    allocator_state.stats.used_memory -= block->size;
    allocator_state.stats.free_memory += block->size;
    allocator_state.stats.active_allocations--;
}

/* Calculate fragmentation ratio */
static void update_fragmentation_ratio(void) {
    if (allocator_state.stats.free_memory == 0) {
        allocator_state.stats.fragmentation_ratio = 0.0;
        return;
    }
    
    /* Find the largest free block */
    memory_block_t* current = allocator_state.free_list;
    size_t largest_free_block = 0;
    
    while (current != NULL) {
        if (current->is_free && current->size > largest_free_block) {
            largest_free_block = current->size;
        }
        current = current->next;
    }
    
    /* Calculate fragmentation as 1 - (largest_free_block / total_free_memory) */
    allocator_state.stats.fragmentation_ratio = 
        1.0 - ((double)largest_free_block / allocator_state.stats.free_memory);
}

/* Public API Implementations */

bool allocator_init(allocator_config_t config) {
    /* Check if already initialized */
    if (allocator_state.initialized) {
        return false;
    }
    
    /* Store configuration */
    allocator_state.config = config;
    
    /* Get system page size if not specified */
    if (config.page_size == 0) {
        allocator_state.page_size = (size_t)sysconf(_SC_PAGESIZE);
    } else {
        allocator_state.page_size = config.page_size;
    }
    
    /* Ensure heap size is page-aligned */
    size_t aligned_heap_size = round_up_to_page_size(config.initial_heap_size);
    
    /* Allocate initial heap */
    void* initial_heap = mmap(NULL, aligned_heap_size, 
                             PROT_READ | PROT_WRITE, 
                             MAP_PRIVATE | MAP_ANONYMOUS, 
                             -1, 0);
    
    if (initial_heap == MAP_FAILED) {
        return false;
    }
    
    /* Initialize mutex */
    if (pthread_mutex_init(&allocator_state.mutex, NULL) != 0) {
        munmap(initial_heap, aligned_heap_size);
        return false;
    }
    
    /* Initialize the heap with a single free block */
    memory_block_t* initial_block = (memory_block_t*)initial_heap;
    initial_block->size = aligned_heap_size;
    initial_block->is_free = true;
    initial_block->protection = MEM_READ | MEM_WRITE;
    initial_block->next = NULL;
    initial_block->prev = NULL;
    
    /* Set up allocator state */
    allocator_state.heap_start = initial_heap;
    allocator_state.heap_size = aligned_heap_size;
    allocator_state.free_list = initial_block;
    allocator_state.used_list = NULL;
    allocator_state.initialized = true;
    
    /* Initialize statistics */
    allocator_state.stats.total_memory = aligned_heap_size;
    allocator_state.stats.used_memory = 0;
    allocator_state.stats.free_memory = aligned_heap_size;
    allocator_state.stats.overhead = sizeof(memory_block_t);
    allocator_state.stats.peak_usage = 0;
    allocator_state.stats.total_allocations = 0;
    allocator_state.stats.active_allocations = 0;
    allocator_state.stats.fragmentation_ratio = 0.0;
    
    return true;
}

void* mem_alloc(size_t size) {
    if (size == 0 || !allocator_state.initialized) {
        return NULL;
    }
    
    /* Calculate total size needed including header */
    size_t total_size = calculate_total_size(size);
    
    /* Lock the mutex for thread safety */
    pthread_mutex_lock(&allocator_state.mutex);
    
    /* Find a suitable free block */
    memory_block_t* block = find_free_block(total_size);
    
    /* If no suitable block found, extend the heap */
    if (block == NULL) {
        block = extend_heap(total_size);
        if (block == NULL) {
            pthread_mutex_unlock(&allocator_state.mutex);
            return NULL;
        }
    }
    
    /* Split the block if it's much larger than needed */
    split_block(block, total_size);
    
    /* Mark the block as used */
    mark_block_used(block);
    
    /* Update fragmentation ratio */
    update_fragmentation_ratio();
    
    /* Unlock the mutex */
    pthread_mutex_unlock(&allocator_state.mutex);
    
    /* Return pointer to the user data area (after the header) */
    return (void*)((char*)block + sizeof(memory_block_t));
}

void* mem_alloc_aligned(size_t size, size_t alignment) {
    if (size == 0 || !allocator_state.initialized || alignment == 0) {
        return NULL;
    }
    
    /* Ensure alignment is a power of 2 */
    if ((alignment & (alignment - 1)) != 0) {
        return NULL;
    }
    
    /* Allocate extra space to ensure we can align the result */
    size_t padding = alignment + sizeof(void*);
    void* raw_ptr = mem_alloc(size + padding);
    
    if (raw_ptr == NULL) {
        return NULL;
    }
    
    /* Calculate aligned address */
    uintptr_t addr = (uintptr_t)raw_ptr;
    uintptr_t aligned_addr = (addr + sizeof(void*) + alignment - 1) & ~(alignment - 1);
    
    /* Store the original pointer just before the aligned memory */
    void** ptr_to_original = (void**)(aligned_addr - sizeof(void*));
    *ptr_to_original = raw_ptr;
    
    return (void*)aligned_addr;
}

void mem_free(void* ptr) {
    if (ptr == NULL || !allocator_state.initialized) {
        return;
    }
    
    /* Lock the mutex for thread safety */
    pthread_mutex_lock(&allocator_state.mutex);
    
    /* Get the block header from the user pointer */
    memory_block_t* block = (memory_block_t*)((char*)ptr - sizeof(memory_block_t));
    
    /* Validate that this is a legitimate block */
    memory_block_t* current = allocator_state.used_list;
    bool found = false;
    
    while (current != NULL) {
        if (current == block) {
            found = true;
            break;
        }
        current = current->next;
    }
    
    if (!found) {
        /* This pointer was not allocated by our allocator */
        pthread_mutex_unlock(&allocator_state.mutex);
        return;
    }
    
    /* Mark the block as free */
    mark_block_free(block);
    
    /* Try to merge adjacent free blocks */
    merge_free_blocks();
    
    /* Update fragmentation ratio */
    update_fragmentation_ratio();
    
    /* Unlock the mutex */
    pthread_mutex_unlock(&allocator_state.mutex);
}

void* mem_realloc(void* ptr, size_t size) {
    if (ptr == NULL) {
        return mem_alloc(size);
    }
    
    if (size == 0) {
        mem_free(ptr);
        return NULL;
    }
    
    if (!allocator_state.initialized) {
        return NULL;
    }
    
    /* Lock the mutex for thread safety */
    pthread_mutex_lock(&allocator_state.mutex);
    
    /* Get the block header from the user pointer */
    memory_block_t* block = (memory_block_t*)((char*)ptr - sizeof(memory_block_t));
    
    /* Calculate total size needed including header */
    size_t total_size = calculate_total_size(size);
    
    /* If new size is smaller, we can just shrink the current block */
    if (total_size <= block->size) {
        /* Split the block if it's much larger than needed */
        split_block(block, total_size);
        
        /* Update fragmentation ratio */
        update_fragmentation_ratio();
        
        pthread_mutex_unlock(&allocator_state.mutex);
        return ptr;
    }
    
    /* If next block is free and contiguous, we might be able to expand in place */
    if (block->next != NULL && block->next->is_free && 
        (block->size + block->next->size >= total_size)) {
        
        /* Combine with the next block */
        size_t combined_size = block->size + block->next->size;
        
        /* Remove the next block from the free list */
        memory_block_t* next_block = block->next;
        if (allocator_state.free_list == next_block) {
            allocator_state.free_list = next_block->next;
        } else {
            memory_block_t* current = allocator_state.free_list;
            while (current != NULL && current->next != next_block) {
                current = current->next;
            }
            
            if (current != NULL) {
                current->next = next_block->next;
                if (next_block->next != NULL) {
                    next_block->next->prev = current;
                }
            }
        }
        
        /* Update block size and pointers */
        block->size = combined_size;
        block->next = next_block->next;
        if (next_block->next != NULL) {
            next_block->next->prev = block;
        }
        
        /* Split if necessary */
        split_block(block, total_size);
        
        /* Update statistics */
        allocator_state.stats.free_memory -= (total_size - block->size);
        allocator_state.stats.used_memory += (total_size - block->size);
        
        /* Update fragmentation ratio */
        update_fragmentation_ratio();
        
        pthread_mutex_unlock(&allocator_state.mutex);
        return ptr;
    }
    
    /* Unlock mutex before calling other functions */
    pthread_mutex_unlock(&allocator_state.mutex);
    
    /* Otherwise, allocate a new block and copy data */
    void* new_ptr = mem_alloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }
    
    /* Calculate user data size in the original block */
    size_t user_size = block->size - sizeof(memory_block_t);
    
    /* Copy data from old block to new block */
    memcpy(new_ptr, ptr, user_size < size ? user_size : size);
    
    /* Free the old block */
    mem_free(ptr);
    
    return new_ptr;
}

bool mem_protect(void* ptr, size_t size, int protection) {
    if (ptr == NULL || size == 0 || !allocator_state.initialized) {
        return false;
    }
    
    /* Lock the mutex for thread safety */
    pthread_mutex_lock(&allocator_state.mutex);
    
    /* Convert protection flags to mprotect flags */
    int prot = 0;
    if (protection & MEM_READ) prot |= PROT_READ;
    if (protection & MEM_WRITE) prot |= PROT_WRITE;
    if (protection & MEM_EXEC) prot |= PROT_EXEC;
    
    /* Round address down to page boundary */
    void* page_addr = (void*)((uintptr_t)ptr & ~(allocator_state.page_size - 1));
    
    /* Calculate total size from the page boundary */
    size_t page_size = (size + ((uintptr_t)ptr - (uintptr_t)page_addr) +
                      allocator_state.page_size - 1) & ~(allocator_state.page_size - 1);
    
    /* Change protection */
    int result = mprotect(page_addr, page_size, prot);
    
    /* Find the block and update its protection flags if successful */
    if (result == 0) {
        memory_block_t* block = (memory_block_t*)((char*)ptr - sizeof(memory_block_t));
        block->protection = protection;
    }
    
    /* Unlock the mutex */
    pthread_mutex_unlock(&allocator_state.mutex);
    
    return (result == 0);
}

allocator_stats_t allocator_get_stats(void) {
    allocator_stats_t stats;
    
    if (!allocator_state.initialized) {
        memset(&stats, 0, sizeof(allocator_stats_t));
        return stats;
    }
    
    /* Lock the mutex for thread safety */
    pthread_mutex_lock(&allocator_state.mutex);
    
    /* Copy statistics */
    stats = allocator_state.stats;
    
    /* Unlock the mutex */
    pthread_mutex_unlock(&allocator_state.mutex);
    
    return stats;
}

void allocator_print_memory_map(void) {
    if (!allocator_state.initialized) {
        printf("Allocator not initialized\n");
        return;
    }
    
    /* Lock the mutex for thread safety */
    pthread_mutex_lock(&allocator_state.mutex);
    
    printf("===== Memory Allocator Map =====\n");
    printf("Total memory: %zu bytes\n", allocator_state.stats.total_memory);
    printf("Used memory: %zu bytes\n", allocator_state.stats.used_memory);
    printf("Free memory: %zu bytes\n", allocator_state.stats.free_memory);
    printf("Fragmentation: %.2f%%\n", allocator_state.stats.fragmentation_ratio * 100.0);
    printf("\n");
    
    /* Print all blocks (both used and free) in address order */
    printf("Address               | Size     | Status | Protection\n");
    printf("----------------------|----------|--------|------------\n");
    
    /* Start with the lowest address */
    memory_block_t* current = allocator_state.free_list;
    memory_block_t* used = allocator_state.used_list;
    
    /* Find the block with the lowest address */
    memory_block_t* all_blocks[1000]; /* Arbitrary limit */
    int block_count = 0;
    
    /* Add all free blocks */
    while (current != NULL && block_count < 1000) {
        all_blocks[block_count++] = current;
        current = current->next;
    }
    
    /* Add all used blocks */
    while (used != NULL && block_count < 1000) {
        all_blocks[block_count++] = used;
        used = used->next;
    }
    
    /* Sort blocks by address */
    for (int i = 0; i < block_count - 1; i++) {
        for (int j = 0; j < block_count - i - 1; j++) {
            if (all_blocks[j] > all_blocks[j + 1]) {
                memory_block_t* temp = all_blocks[j];
                all_blocks[j] = all_blocks[j + 1];
                all_blocks[j + 1] = temp;
            }
        }
    }
    
    /* Print sorted blocks */
    for (int i = 0; i < block_count; i++) {
        memory_block_t* block = all_blocks[i];
        printf("0x%016lx | %-8zu | %-6s | ", 
               (unsigned long)block, 
               block->size, 
               block->is_free ? "FREE" : "USED");
        
        /* Print protection flags */
        if (block->protection & MEM_READ) printf("R");
        else printf("-");
        if (block->protection & MEM_WRITE) printf("W");
        else printf("-");
        if (block->protection & MEM_EXEC) printf("X");
        else printf("-");
        
        printf("\n");
    }
    
    printf("==============================\n");
    
    /* Unlock the mutex */
    pthread_mutex_unlock(&allocator_state.mutex);
}

void allocator_cleanup(void) {
    if (!allocator_state.initialized) {
        return;
    }
    
    /* Lock the mutex for thread safety */
    pthread_mutex_lock(&allocator_state.mutex);
    
    /* Unmap all memory */
    if (allocator_state.heap_start != NULL) {
        munmap(allocator_state.heap_start, allocator_state.heap_size);
        allocator_state.heap_start = NULL;
    }
    
    /* Reset all state variables */
    allocator_state.heap_size = 0;
    allocator_state.free_list = NULL;
    allocator_state.used_list = NULL;
    allocator_state.initialized = false;
    
    /* Zero out statistics */
    memset(&allocator_state.stats, 0, sizeof(allocator_stats_t));
    
    /* Unlock and destroy the mutex */
    pthread_mutex_unlock(&allocator_state.mutex);
    pthread_mutex_destroy(&allocator_state.mutex);
}

int main() {
    // Initialize the allocator with default configuration
    allocator_config_t config = {
        .initial_heap_size = 1024 * 1024,  // 1MB initial heap
        .page_size = 0,                    // Use system default
        .allocation_strategy = 1           // Best fit strategy
    };
    
    if (!allocator_init(config)) {
        printf("Failed to initialize allocator\n");
        return 1;
    }
    
    // Test allocation
    void* ptr1 = mem_alloc(100);
    void* ptr2 = mem_alloc(200);
    void* ptr3 = mem_alloc(300);
    
    // Print memory map
    allocator_print_memory_map();
    
    // Free some memory
    mem_free(ptr2);
    
    // Print memory map after freeing
    printf("\nAfter freeing ptr2:\n");
    allocator_print_memory_map();
    
    // Cleanup
    mem_free(ptr1);
    mem_free(ptr3);
    allocator_cleanup();
    
    return 0;
}