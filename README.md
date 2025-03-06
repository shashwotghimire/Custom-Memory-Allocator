# Custom Memory Allocator

This project implements a custom memory allocator in C that provides dynamic memory management capabilities with various allocation strategies and memory protection features.

## Features

- Multiple allocation strategies (First Fit, Best Fit, Worst Fit)
- Thread-safe memory operations
- Memory protection controls (Read/Write/Execute)
- Memory alignment support
- Memory usage statistics and fragmentation tracking
- Memory mapping and debugging tools

## Project Structure

```
custom_memory_allocator/
├── include/
│   └── allocator.h       # Header file with public API declarations
├── src/
│   ├── allocator.c       # Main implementation file
│   ├── benchmark_allocator.c  # Benchmark comparison with malloc
│   └── test_allocator.c  # Test suite
└── README.md
```

## API Overview

```c
bool allocator_init(allocator_config_t config);
void* mem_alloc(size_t size);
void* mem_alloc_aligned(size_t size, size_t alignment);
void mem_free(void* ptr);
void* mem_realloc(void* ptr, size_t size);
bool mem_protect(void* ptr, size_t size, int protection);
allocator_stats_t allocator_get_stats(void);
void allocator_print_memory_map(void);
void allocator_cleanup(void);
```

## Building and Testing

To build the project:

```bash
gcc -o allocator src/allocator.c -pthread
gcc -o test_allocator src/test_allocator.c src/allocator.c -pthread
gcc -o benchmark src/benchmark_allocator.c src/allocator.c -pthread
```

To run tests:

```bash
./test_allocator
```

To run benchmarks:

```bash
./benchmark
```

## Performance

The allocator includes benchmarking tools to compare its performance against the standard malloc implementation. Metrics include:

- Allocation/deallocation speed
- Memory fragmentation
- Peak memory usage
- Memory overhead

## License

This project is open source and available under the MIT License.
