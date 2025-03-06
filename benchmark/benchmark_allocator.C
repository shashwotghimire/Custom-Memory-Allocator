#include "../include/allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#define NUM_ALLOCATIONS 10000
#define MAX_ALLOCATION_SIZE 1024
#define NUM_ITERATIONS 5

/* Timing helper function */
double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

/* Benchmark structure */
typedef struct {
    double alloc_time;
    double free_time;
    double fragmentation;
    size_t peak_memory;
} benchmark_result_t;

/* Run single benchmark iteration */
benchmark_result_t run_custom_allocator_benchmark() {
    benchmark_result_t result = {0};
    void* ptrs[NUM_ALLOCATIONS] = {0};
    double start_time, end_time;
    
    /* Allocation phase */
    start_time = get_time();
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        size_t size = rand() % MAX_ALLOCATION_SIZE + 1;
        ptrs[i] = mem_alloc(size);
    }
    end_time = get_time();
    result.alloc_time = end_time - start_time;
    
    /* Get peak memory and fragmentation */
    allocator_stats_t stats = allocator_get_stats();
    result.peak_memory = stats.peak_usage;
    result.fragmentation = stats.fragmentation_ratio;
    
    /* Free phase */
    start_time = get_time();
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        if (ptrs[i] != NULL) {
            mem_free(ptrs[i]);
        }
    }
    end_time = get_time();
    result.free_time = end_time - start_time;
    
    return result;
}

/* Run malloc benchmark for comparison */
benchmark_result_t run_malloc_benchmark() {
    benchmark_result_t result = {0};
    void* ptrs[NUM_ALLOCATIONS] = {0};
    double start_time, end_time;
    
    start_time = get_time();
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        size_t size = rand() % MAX_ALLOCATION_SIZE + 1;
        ptrs[i] = malloc(size);
    }
    end_time = get_time();
    result.alloc_time = end_time - start_time;
    
    start_time = get_time();
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        if (ptrs[i] != NULL) {
            free(ptrs[i]);
        }
    }
    end_time = get_time();
    result.free_time = end_time - start_time;
    
    return result;
}

int main() {
    srand(time(NULL));
    
    /* Initialize our allocator */
    allocator_config_t config = {
        .initial_heap_size = 16 * 1024 * 1024,  // 16MB initial heap
        .page_size = 0,                         // Use system default
        .allocation_strategy = 1                // Best fit strategy
    };
    
    if (!allocator_init(config)) {
        printf("Failed to initialize custom allocator\n");
        return 1;
    }
    
    printf("Running benchmarks (%d iterations of %d allocations)...\n\n", 
           NUM_ITERATIONS, NUM_ALLOCATIONS);
    
    /* Results storage */
    benchmark_result_t custom_results[NUM_ITERATIONS];
    benchmark_result_t malloc_results[NUM_ITERATIONS];
    
    /* Run multiple iterations */
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        printf("Iteration %d/%d...\n", i + 1, NUM_ITERATIONS);
        
        custom_results[i] = run_custom_allocator_benchmark();
        malloc_results[i] = run_malloc_benchmark();
    }
    
    /* Calculate averages */
    double avg_custom_alloc = 0, avg_custom_free = 0, avg_custom_frag = 0;
    double avg_malloc_alloc = 0, avg_malloc_free = 0;
    size_t avg_peak_mem = 0;
    
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        avg_custom_alloc += custom_results[i].alloc_time;
        avg_custom_free += custom_results[i].free_time;
        avg_custom_frag += custom_results[i].fragmentation;
        avg_peak_mem += custom_results[i].peak_memory;
        
        avg_malloc_alloc += malloc_results[i].alloc_time;
        avg_malloc_free += malloc_results[i].free_time;
    }
    
    avg_custom_alloc /= NUM_ITERATIONS;
    avg_custom_free /= NUM_ITERATIONS;
    avg_custom_frag /= NUM_ITERATIONS;
    avg_peak_mem /= NUM_ITERATIONS;
    avg_malloc_alloc /= NUM_ITERATIONS;
    avg_malloc_free /= NUM_ITERATIONS;
    
    /* Print results */
    printf("\nBenchmark Results:\n");
    printf("=================\n\n");
    
    printf("Custom Allocator:\n");
    printf("  Average allocation time: %.6f seconds\n", avg_custom_alloc);
    printf("  Average free time: %.6f seconds\n", avg_custom_free);
    printf("  Average fragmentation: %.2f%%\n", avg_custom_frag * 100.0);
    printf("  Average peak memory: %zu bytes\n", avg_peak_mem);
    printf("\n");
    
    printf("Standard Malloc:\n");
    printf("  Average allocation time: %.6f seconds\n", avg_malloc_alloc);
    printf("  Average free time: %.6f seconds\n", avg_malloc_free);
    printf("\n");
    
    printf("Performance Ratio (Custom/Malloc):\n");
    printf("  Allocation time ratio: %.2fx\n", avg_custom_alloc / avg_malloc_alloc);
    printf("  Free time ratio: %.2fx\n", avg_custom_free / avg_malloc_free);
    
    /* Cleanup */
    allocator_cleanup();
    
    return 0;
}
