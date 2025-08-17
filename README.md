

```markdown
# memdebug

A lightweight memory debugging and leak detection library for C applications.

## How It Works

memdebug wraps standard memory allocation functions to track memory usage:
- Every allocation (malloc, calloc, realloc) is recorded in a hash table
- Each entry stores the pointer, size, file name, and line number
- Optional: captures a backtrace for each allocation
- When memory is freed, the corresponding entry is removed
- At program exit, any remaining entries indicate memory leaks with origin info
- Tracks current, peak, counts, and summaries

## Features

- Detects memory leaks with file and line information
- Tracks total and peak memory usage, allocation/free counts, outstanding blocks
- Handles realloc operations correctly
- Supports malloc, calloc, realloc, and free
- Thread-safe via internal mutex
- Optional backtrace capture and leak stack printing
- Works in two modes:
  - Macro mode (compile your sources with the provided header/macros)
  - Interposition mode (LD_PRELOAD) to detect leaks from third-party libraries too
- Minimal overhead during normal operation

## Compilation

```bash
# Compile the library (macro mode)
clang -O3 -c memory.c -o memory.o -pthread

# Compile your program with the library
clang -O3 your_program.c memory.o -o your_program -pthread
```

### Interposition Mode (detect leaks in third-party libraries)

Build as a shared object and preload it so all allocations are tracked, even inside libraries:

```bash
clang -O2 -fPIC -shared -DMEMDEBUG_INTERPOSE memory.c -o libmemdebug.so -ldl -pthread

# Run your program with the interposer
LD_PRELOAD=$PWD/libmemdebug.so MEMDEBUG=1 ./your_program
```

Notes:
- Do not mix macro mode and LD_PRELOAD simultaneously.
- On glibc systems, `LD_PRELOAD` interposes `malloc/calloc/realloc/free` globally.

## Usage (Macro Mode)

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
memdebug_dump_leaks();  // Print all leaks to stderr or configured log file
memdebug_finalize();    // Clean up internal structures
```

5. Optionally check current/peak usage and stats:
```c
size_t bytes = memdebug_get_allocated();
size_t peak  = memdebug_get_peak_allocated();
size_t outstanding = memdebug_get_outstanding_count();
```

## Runtime Configuration (env vars)

- `MEMDEBUG=0|1` — enable/disable tracking (default 1)
- `MEMDEBUG_LOG=/path/to/file` — write logs to file (default stderr)
- `MEMDEBUG_BACKTRACE=0|1` — capture stack on alloc (default 0)
- `MEMDEBUG_BT_DEPTH=N` — backtrace depth (default 12, max 16)
- `MEMDEBUG_BUCKETS=N` — hash buckets (default 4096)
- `MEMDEBUG_ABORT_ON_LEAK=0|1` — call abort() if leaks detected at finalize (default 0)

## Testing

Compile and run the test program:
```bash
clang -O3 test.c memory.o -o memtest -pthread
./memtest
```

Example output:
```
Currently allocated: 141 bytes
Leak: 0x55d8b5d972a0 (16 bytes) at test.c:22
Leak: 0x55d8b5d97300 (16 bytes) at test.c:22
Leak: 0x55d8b5d97360 (100 bytes) at test.c:30
Leak: 0x55d8b5d973d0 (4 bytes) at test.c:48
Leak: 0x55d8b5d973f0 (5 bytes) at test.c:49
Summary: outstanding=5, total_allocs=..., total_frees=..., current_bytes=..., peak_bytes=..., leaks=5
```

## Limitations

- Backtraces require `execinfo.h`; symbolization quality depends on debug info
- Interposition relies on `LD_PRELOAD` (POSIX/glibc); not applicable on all platforms
- Does not detect buffer overflows/underflows (use ASan for that)

## License

MIT License - feel free to use in any project.
```
