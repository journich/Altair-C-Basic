# Altair 8K BASIC 4.0 - C17 Implementation

A clean-room C17 implementation of Microsoft's Altair 8K BASIC 4.0, designed for 100% compatibility with the original 1976 interpreter.

## Overview

This project reimplements the historic Altair 8K BASIC interpreter in modern, portable C17. The goal is byte-for-byte output compatibility with the original 8080 assembly version, allowing vintage BASIC programs (including David Ahl's "BASIC Computer Games") to run identically.

### Features

- **Microsoft Binary Format (MBF)** floating-point arithmetic (not IEEE 754)
- **Exact RND algorithm** reproduction using the original two-stage table-based method
- **Complete tokenizer** with all 70 BASIC keywords
- **Full expression parser** with correct operator precedence
- **All BASIC statements**: PRINT, INPUT, FOR/NEXT, GOSUB/RETURN, IF/THEN, etc.
- **All BASIC functions**: Mathematical, string, and system functions
- **Program storage** with line editing and LIST command
- **Cross-platform**: Builds on macOS, Linux, and Windows

## Building

### Requirements

- CMake 3.16 or later
- C17-compatible compiler (GCC, Clang, or MSVC)

### Build Instructions

```bash
mkdir build && cd build
cmake ..
make
```

### Running Tests

```bash
cd build
ctest --output-on-failure
```

## Usage

```bash
./basic8k
```

The interpreter will display the classic startup banner:

```
ALTAIR BASIC REV. 4.0
[8K VERSION]
COPYRIGHT 1976 BY MITS INC.

65279 BYTES FREE

OK
```

### Commands

| Command | Description |
|---------|-------------|
| `NEW` | Clear program and variables |
| `RUN` | Execute program |
| `LIST` | Display program |
| `LIST 10-50` | List lines 10 through 50 |
| `CONT` | Continue after STOP |
| `CLEAR` | Clear variables |

### Statements

| Statement | Description |
|-----------|-------------|
| `LET A=5` | Assignment (LET optional) |
| `PRINT` | Output to terminal |
| `INPUT` | Read user input |
| `IF...THEN` | Conditional execution |
| `FOR...TO...STEP` | Loop construct |
| `NEXT` | End of FOR loop |
| `GOTO` | Unconditional jump |
| `GOSUB/RETURN` | Subroutine call |
| `DIM` | Dimension arrays |
| `DATA/READ/RESTORE` | Data statements |
| `DEF FN` | Define function |
| `REM` | Comment |
| `END/STOP` | End execution |
| `POKE` | Write to memory |
| `ON...GOTO/GOSUB` | Computed jump |

### Functions

| Function | Description |
|----------|-------------|
| `ABS(X)` | Absolute value |
| `SGN(X)` | Sign (-1, 0, 1) |
| `INT(X)` | Integer part (floor) |
| `SQR(X)` | Square root |
| `SIN(X)`, `COS(X)`, `TAN(X)` | Trigonometric |
| `ATN(X)` | Arctangent |
| `LOG(X)` | Natural logarithm |
| `EXP(X)` | Exponential (e^x) |
| `RND(X)` | Random number |
| `LEN(A$)` | String length |
| `LEFT$(A$,N)` | Left substring |
| `RIGHT$(A$,N)` | Right substring |
| `MID$(A$,S,N)` | Middle substring |
| `ASC(A$)` | ASCII code |
| `CHR$(N)` | Character from code |
| `STR$(X)` | Number to string |
| `VAL(A$)` | String to number |
| `INSTR(S,A$,B$)` | Find substring |
| `PEEK(A)` | Read memory |
| `FRE(X)` | Free memory |
| `POS(X)` | Cursor position |

## Project Structure

```
basic8k_c/
├── CMakeLists.txt          # Build configuration
├── LICENSE                 # MIT License
├── README.md               # This file
├── include/basic/
│   ├── basic.h             # Main API and types
│   ├── mbf.h               # MBF floating-point
│   ├── tokens.h            # Token definitions
│   └── errors.h            # Error codes
├── src/
│   ├── main.c              # Entry point
│   ├── core/
│   │   ├── interpreter.c   # Main loop
│   │   ├── tokenizer.c     # Keyword tokenization
│   │   ├── parser.c        # Expression parser
│   │   └── evaluator.c     # Expression evaluation
│   ├── math/
│   │   ├── mbf.c           # MBF core operations
│   │   ├── mbf_arith.c     # Add/Sub/Mul/Div
│   │   ├── mbf_trig.c      # Transcendental functions
│   │   └── rnd.c           # RND implementation
│   ├── memory/
│   │   ├── program.c       # Program storage
│   │   ├── variables.c     # Variable storage
│   │   ├── arrays.c        # Array handling
│   │   └── strings.c       # String space
│   ├── statements/
│   │   ├── flow.c          # Control flow
│   │   ├── io.c            # I/O statements
│   │   └── misc.c          # Other statements
│   ├── functions/
│   │   ├── numeric.c       # Numeric functions
│   │   └── string.c        # String functions
│   └── io/
│       └── terminal.c      # Terminal handling
└── tests/
    ├── unit/               # Unit tests
    └── test_harness.h      # Test framework
```

## Technical Details

### Microsoft Binary Format (MBF)

Unlike modern IEEE 754 floating-point, MBF uses:
- 4 bytes per number
- Exponent bias of 129 (not 127)
- Sign bit in the mantissa byte
- Different normalization rules

This implementation provides exact MBF arithmetic to match original output.

### RND Algorithm

The random number generator uses a two-stage algorithm with lookup tables:
1. Multiply seed by value from 8-entry multiplier table
2. Add value from 4-entry addend table
3. Apply specific bit manipulations

This produces the exact same sequence as the original for compatibility.

### Memory Layout

```
+------------------+ 0x0000
| Program Area     |
| (tokenized code) |
+------------------+ program_end / var_start
| Simple Variables |
| (6 bytes each)   |
+------------------+ array_start
| Arrays           |
| (headers + data) |
+------------------+
| Free Space       |
+------------------+ string_start
| String Space     |
| (grows down)     |
+------------------+ string_end / memory_size
```

## Hardware Emulation

The original 8K BASIC included hardware-specific features for the Altair 8800:

| Feature | Implementation |
|---------|----------------|
| `INP(port)` | Returns 0 with warning |
| `OUT port,value` | No-op with warning |
| `WAIT port,mask` | No-op with warning |
| `USR(addr)` | Returns 0 with warning |
| `PEEK(addr)` | Reads from interpreter memory |
| `POKE addr,value` | Writes to interpreter memory |

## Development Status

- [x] Phase 1-4: MBF math, RND, Tokenizer, Parser
- [x] Phase 5: Variables, arrays, string space
- [x] Phase 6: Statement implementations
- [x] Phase 7: Function implementations
- [ ] Phase 8: Main interpreter loop
- [ ] Phase 9: Compatibility testing

## Testing

The project includes comprehensive unit tests:

```bash
cd build
ctest -V
```

Current test coverage:
- 21 MBF arithmetic tests
- 8 RND sequence tests
- 18 Tokenizer tests
- 14 Parser tests
- 25 Memory management tests

## License

MIT License

Copyright (c) 2025 Tim Buchalka

Based on Altair 8K BASIC 4.0
Copyright (c) 1976 Microsoft (Bill Gates, Paul Allen, Monte Davidoff)

See [LICENSE](LICENSE) for full text.

## Acknowledgments

- **Bill Gates, Paul Allen, Monte Davidoff** - Original Altair BASIC authors
- **MITS (Micro Instrumentation and Telemetry Systems)** - Original hardware platform
- The retrocomputing community for preserving this historic software

## References

- [Altair 8800](https://en.wikipedia.org/wiki/Altair_8800)
- [Microsoft BASIC](https://en.wikipedia.org/wiki/Microsoft_BASIC)
- [BASIC Computer Games](https://en.wikipedia.org/wiki/BASIC_Computer_Games)
