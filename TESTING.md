# Testing Guide

This document describes the testing infrastructure for the Altair 8K BASIC C implementation.

## Overview

The project uses two types of tests:

1. **Unit Tests** - Fast C tests for individual components (MBF math, RND, tokenizer, parser, memory)
2. **Compatibility Tests** - End-to-end tests comparing C output against the original 8080 BASIC via SIMH

## Quick Start

```bash
# Build the project
cd build
cmake ..
make

# Run unit tests
ctest --output-on-failure

# Run compatibility tests (C implementation only)
cd ../compatibility_tests/scripts
./test_harness.py c-only

# Run full compatibility tests (requires SIMH)
./test_harness.py test
```

---

## Unit Tests

### Location

```
tests/
├── CMakeLists.txt      # Test build configuration
├── test_harness.h      # Minimal test framework
└── unit/
    ├── test_mbf.c      # MBF floating-point tests
    ├── test_rnd.c      # Random number generator tests
    ├── test_tokenizer.c # Tokenizer tests
    ├── test_parser.c   # Expression parser tests
    └── test_memory.c   # Memory management tests
```

### Running Unit Tests

```bash
cd build

# Run all tests
ctest

# Run with verbose output
ctest -V

# Run specific test
./tests/test_mbf
./tests/test_rnd
```

### Test Framework

Unit tests use a minimal embedded framework (`test_harness.h`) with these macros:

| Macro | Description |
|-------|-------------|
| `TEST(name)` | Declare a test function |
| `RUN_TEST(name)` | Execute a test |
| `ASSERT(cond)` | Assert condition is true |
| `ASSERT_EQ(a, b)` | Assert equality |
| `ASSERT_EQ_INT(a, b)` | Assert integers equal (with values in error) |
| `ASSERT_EQ_HEX(a, b)` | Assert hex values equal |
| `ASSERT_STR_EQ(a, b)` | Assert strings equal |
| `ASSERT_MBF_EQ(a, b)` | Assert MBF values equal |
| `TEST_MAIN()` | Generate main() function |

### Example Unit Test

```c
#include "test_harness.h"
#include "basic/mbf.h"

TEST(test_mbf_add_simple) {
    mbf_t a = mbf_from_int16(2);
    mbf_t b = mbf_from_int16(3);
    mbf_t result = mbf_add(a, b);
    mbf_t expected = mbf_from_int16(5);
    ASSERT_MBF_EQ(result, expected);
}

static void run_tests(void) {
    RUN_TEST(test_mbf_add_simple);
}

TEST_MAIN()
```

---

## Compatibility Tests

Compatibility tests verify the C implementation produces identical output to the original 8080 BASIC running in the SIMH Altair emulator.

### Directory Structure

```
compatibility_tests/
├── programs/           # BASIC test programs (.bas files)
│   ├── 01_arithmetic.bas
│   ├── 02_numeric_functions.bas
│   └── ...
├── golden/             # Expected outputs from 8080 BASIC (.golden files)
│   ├── 01_arithmetic.golden
│   ├── 02_numeric_functions.golden
│   └── ...
├── results/            # Generated test outputs (NOT in git)
│   ├── *.actual        # SIMH output
│   ├── *.c_output      # C implementation output
│   └── *.diff          # Differences between golden and actual
└── scripts/
    ├── test_harness.py     # Main test runner
    ├── run_c_test.sh       # Run single C test
    ├── run_simh_test.exp   # Run test via SIMH
    ├── run_simh_single.exp # Run single SIMH test
    └── generate_golden.exp # Generate golden output
```

### Test Program Format

Test programs use `===` markers to delimit output for comparison:

```basic
10 PRINT "=== TEST NAME ==="
20 REM ... test code ...
90 PRINT "=== END TEST NAME ==="
100 END
```

Example (`01_arithmetic.bas`):
```basic
1 REM *** ARITHMETIC COMPATIBILITY TEST ***
10 PRINT "=== BASIC ARITHMETIC ==="
20 PRINT "2+3=";2+3
30 PRINT "10-7=";10-7
...
150 PRINT "=== END ARITHMETIC ==="
160 END
```

### Golden Files

Golden files contain the expected output captured from the original 8080 BASIC via SIMH. They are the **source of truth** for compatibility.

Example (`01_arithmetic.golden`):
```
=== BASIC ARITHMETIC ===
2+3= 5
10-7= 3
6*7= 42
...
=== END ARITHMETIC ===
```

### Generated Files (Not Tracked in Git)

When tests run, these files are generated in `compatibility_tests/results/`:

| File Pattern | Description |
|--------------|-------------|
| `*.actual` | Raw output from SIMH (8080 BASIC) |
| `*.c_output` | Output from C implementation |
| `*.diff` | Unified diff between golden and actual |

These files are in `.gitignore` because they are regenerated on each test run.

### Running Compatibility Tests

```bash
cd compatibility_tests/scripts

# Run C implementation tests only (fast, no SIMH needed)
./test_harness.py c-only

# Run full comparison against golden files
./test_harness.py test

# Regenerate golden files from SIMH (slow)
./test_harness.py generate

# Do both: regenerate golden and run tests
./test_harness.py all
```

### Test Harness Commands

| Command | Description |
|---------|-------------|
| `c-only` | Run C tests, verify they produce output (no golden comparison) |
| `test` | Compare C output against golden files |
| `generate` | Capture new golden outputs from SIMH |
| `all` | Generate golden files, then run tests |

### Test Results

```
Running compatibility tests...
============================================================
  [✓] 01_arithmetic: PASS
  [✓] 02_numeric_functions: PASS
  [✓] 03_trig_functions: PASS
  [✗] 06_rnd_sequence: FAIL
  ...
============================================================
Results: 12 passed, 2 failed, 1 skipped
```

---

## Adding New Tests

### Adding a Unit Test

1. Add test function to existing file or create new `tests/unit/test_*.c`
2. For new files, add to `tests/CMakeLists.txt`:
   ```cmake
   add_executable(test_newfeature unit/test_newfeature.c)
   target_link_libraries(test_newfeature PRIVATE basic8k_core test_harness)
   add_test(NAME NewFeature_Tests COMMAND test_newfeature)
   ```
3. Rebuild and run:
   ```bash
   cd build && make && ctest
   ```

### Adding a Compatibility Test

1. Create test program in `compatibility_tests/programs/`:
   ```basic
   10 PRINT "=== MY NEW TEST ==="
   20 REM test code here
   90 PRINT "=== END MY NEW TEST ==="
   100 END
   ```

2. Generate golden output:
   ```bash
   cd compatibility_tests/scripts
   ./test_harness.py generate
   ```

3. Run tests to verify:
   ```bash
   ./test_harness.py test
   ```

---

## RND Testing Tools

Special tools exist for testing the random number generator:

### Generate RND Values from C

```bash
cd build
./gen_rnd_c 100      # Generate 100 RND values
./gen_rnd_c 1000     # Generate 1000 RND values
```

### Capture RND Values from 8080 BASIC

```bash
cd tests
./fast_rnd_1000.exp > /tmp/8080_output.txt
```

This uses SIMH to run `FOR I=1 TO 1000:PRINT RND(1):NEXT` and captures output.

---

## Troubleshooting

### "No golden output file"
Run `./test_harness.py generate` to create golden files from SIMH.

### Tests timeout
SIMH can be slow. Increase timeout in `test_harness.py` or `*.exp` scripts.

### Differences in number formatting
The test harness normalizes whitespace. Floating-point display differences at the last digit (±1 in 6th significant figure) are expected due to MBF precision limits.

### SIMH not found
Install SIMH and ensure `altair` is in your PATH, or update paths in test scripts.

---

## File Summary

| Location | Tracked | Description |
|----------|---------|-------------|
| `tests/unit/*.c` | ✓ | Unit test source code |
| `tests/test_harness.h` | ✓ | Test framework header |
| `compatibility_tests/programs/*.bas` | ✓ | BASIC test programs |
| `compatibility_tests/golden/*.golden` | ✓ | Expected outputs (source of truth) |
| `compatibility_tests/scripts/*.py` | ✓ | Test runner scripts |
| `compatibility_tests/scripts/*.exp` | ✓ | Expect scripts for SIMH |
| `compatibility_tests/results/*` | ✗ | Generated outputs (in .gitignore) |
| `build/tests/*` | ✗ | Compiled test binaries |

---

## Continuous Integration

To run all tests in CI:

```bash
#!/bin/bash
set -e

# Build
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# Unit tests
ctest --output-on-failure

# Compatibility tests (C-only, no SIMH in CI)
cd ../compatibility_tests/scripts
python3 test_harness.py c-only
```

For full compatibility testing, a CI environment with SIMH installed is required.
