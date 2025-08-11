

```markdown
# memdebug

A lightweight memory debugging and leak detection library for C applications.

## How It Works

memdebug wraps standard memory allocation functions to track memory usage:
- Every allocation (malloc, calloc, realloc) is recorded in a linked list
- Each entry stores the pointer, size, file name, and line number
- When memory is freed, the corresponding entry is removed
- At program exit, any remaining entries in the list indicate memory leaks
- The library tracks total allocated memory and reports leaks with their origin

## Features

- Detects memory leaks with file and line information
- Tracks total memory usage
- Handles realloc operations correctly
- Supports malloc, calloc, realloc, and free
- Thread-safe (with proper mutex implementation if needed)
- Minimal overhead during normal operation

## Compilation

```bash
# Compile the library
clang -O3 -c memory.c -o memory.o

# Compile your program with the library
clang -O3 your_program.c memory.o -o your_program
```

## Usage

1. Include the header in your source files:
```c
#include "memory.h"
```

2. Initialize the library at program start:
```c
memdebug_init();
```

3. Replace standard memory functions with memdebug macros:
```c
// Instead of:
ptr = malloc(size);
ptr = calloc(num, size);
ptr = realloc(ptr, new_size);
free(ptr);

// Use:
ptr = MALLOC(size);
ptr = CALLOC(num, size);
ptr = REALLOC(ptr, new_size);
FREE(ptr);
```

4. Check for leaks before program exit:
```c
memdebug_dump_leaks();  // Print all leaks to stdout
memdebug_finalize();    // Clean up internal structures
```

5. Optionally check current memory usage:
```c
size_t bytes = memdebug_get_allocated();
printf("Currently allocated: %zu bytes\n", bytes);
```

## Testing

Compile and run the test program:
```bash
clang -O3 test.c memory.o -o memtest
./memtest
```

The test program includes:
- Basic allocation and freeing
- Linked list with partial freeing (intentional leaks)
- Realloc operations (both growing and shrinking)
- Mixed malloc/calloc usage
- Intentional leaks in separate functions
- Double free protection

Example output:
```
Current allocation: 141 bytes
Leak: 0x55d8b5d972a0 (16 bytes) at test.c:22
Leak: 0x55d8b5d97300 (16 bytes) at test.c:22
Leak: 0x55d8b5d97360 (100 bytes) at test.c:30
Leak: 0x55d8b5d973d0 (4 bytes) at test.c:48
Leak: 0x55d8b5d973f0 (5 bytes) at test.c:49
```

## Limitations

- Doesn't track memory allocated by third-party libraries
- Doesn't detect buffer overflows or underflows
- Adds minimal overhead to memory operations
- Requires explicit initialization and finalization

## License

MIT License - feel free to use in any project.
```
