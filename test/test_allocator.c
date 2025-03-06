#include "../include/allocator.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>   // Add this include for uintptr_t type

/* Test helper functions */
static void test_header(const char* test_name) {
    printf("\n=== Running %s ===\n", test_name);
}

static void test_result(const char* test_name, bool passed) {
    printf("%s: %s\n", test_name, passed ? "PASSED" : "FAILED");
}

/* Test cases */
void test_init() {
    test_header("Initialization Test");
    
    allocator_config_t config = {
        .initial_heap_size = 1024 * 1024,
        .page_size = 0,
        .allocation_strategy = 1
    };
    
    bool result = allocator_init(config);
    test_result("Allocator initialization", result);
    
    allocator_stats_t stats = allocator_get_stats();
    test_result("Initial free memory", stats.free_memory == 1024 * 1024);
    test_result("Initial used memory", stats.used_memory == 0);
    
    allocator_cleanup();
}

void test_basic_allocation() {
    test_header("Basic Allocation Test");
    
    allocator_init((allocator_config_t){.initial_heap_size = 1024 * 1024});
    
    void* ptr = mem_alloc(100);
    test_result("Basic allocation", ptr != NULL);
    
    allocator_stats_t stats = allocator_get_stats();
    test_result("Memory usage tracking", stats.used_memory > 0);
    
    mem_free(ptr);
    allocator_cleanup();
}

void test_multiple_allocations() {
    test_header("Multiple Allocations Test");
    
    allocator_init((allocator_config_t){.initial_heap_size = 1024 * 1024});
    
    void* ptrs[5];
    bool all_valid = true;
    
    for (int i = 0; i < 5; i++) {
        ptrs[i] = mem_alloc(100);
        if (ptrs[i] == NULL) all_valid = false;
        
        // Write some data to ensure memory is accessible
        memset(ptrs[i], i, 100);
    }
    
    test_result("Multiple allocations", all_valid);
    
    // Free in reverse order
    for (int i = 4; i >= 0; i--) {
        mem_free(ptrs[i]);
    }
    
    allocator_stats_t stats = allocator_get_stats();
    test_result("All memory freed", stats.used_memory == 0);
    
    allocator_cleanup();
}

void test_alignment() {
    test_header("Alignment Test");
    
    allocator_init((allocator_config_t){.initial_heap_size = 1024 * 1024});
    
    void* ptr = mem_alloc_aligned(100, 64);
    test_result("Aligned allocation", ptr != NULL);
    test_result("64-byte alignment", ((uintptr_t)ptr % 64) == 0);
    
    mem_free(ptr);
    allocator_cleanup();
}

void test_reallocation() {
    test_header("Reallocation Test");
    
    allocator_init((allocator_config_t){.initial_heap_size = 1024 * 1024});
    
    void* ptr = mem_alloc(100);
    memset(ptr, 0x55, 100);
    
    void* new_ptr = mem_realloc(ptr, 200);
    test_result("Reallocation", new_ptr != NULL);
    
    // Verify original data was preserved
    unsigned char* data = (unsigned char*)new_ptr;
    bool data_preserved = true;
    for (int i = 0; i < 100; i++) {
        if (data[i] != 0x55) {
            data_preserved = false;
            break;
        }
    }
    test_result("Data preservation", data_preserved);
    
    mem_free(new_ptr);
    allocator_cleanup();
}

void test_fragmentation() {
    test_header("Fragmentation Test");
    
    allocator_init((allocator_config_t){.initial_heap_size = 1024 * 1024});
    
    void* ptrs[100];
    
    // Allocate many blocks of different sizes
    for (int i = 0; i < 100; i++) {
        ptrs[i] = mem_alloc((i % 10 + 1) * 32);
    }
    
    // Free every other block to create fragmentation
    for (int i = 0; i < 100; i += 2) {
        mem_free(ptrs[i]);
    }
    
    allocator_stats_t stats = allocator_get_stats();
    printf("Fragmentation ratio: %.2f%%\n", stats.fragmentation_ratio * 100.0);
    
    // Free remaining blocks
    for (int i = 1; i < 100; i += 2) {
        mem_free(ptrs[i]);
    }
    
    allocator_cleanup();
}

void test_protection() {
    test_header("Memory Protection Test");
    
    allocator_init((allocator_config_t){.initial_heap_size = 1024 * 1024});
    
    void* ptr = mem_alloc(100);
    test_result("Protection change", mem_protect(ptr, 100, MEM_READ));
    
    mem_free(ptr);
    allocator_cleanup();
}

int main() {
    printf("Running Memory Allocator Tests\n");
    printf("==============================\n");
    
    test_init();
    test_basic_allocation();
    test_multiple_allocations();
    test_alignment();
    test_reallocation();
    test_fragmentation();
    test_protection();
    
    printf("\nAll tests completed.\n");
    return 0;
}
