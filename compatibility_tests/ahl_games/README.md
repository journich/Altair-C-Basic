# David Ahl's BASIC Computer Games - Compatibility Test Suite

This test suite validates the Altair 8K BASIC C implementation against the 96 classic
games from David Ahl's 1978 book "BASIC Computer Games" - the first million-selling
computer book ever published.

## Purpose

The purpose of this testing framework is to ensure **100% compatibility** between our
C implementation of Altair 8K BASIC and the original Microsoft BASIC interpreter from
1976. By running all 96 games from this historic collection, we can verify:

1. **Correct statement execution** - All BASIC statements work correctly
2. **Numeric precision** - MBF (Microsoft Binary Format) arithmetic matches exactly
3. **Random number generation** - RND produces identical sequences
4. **String handling** - String operations match original behavior
5. **TAB and formatting** - Output formatting matches exactly
6. **INPUT handling** - User input parsing works correctly
7. **Control flow** - GOTO, GOSUB, FOR/NEXT, IF/THEN all work correctly

## Why These Games?

David Ahl's "BASIC Computer Games" is the ideal compatibility test suite because:

- **Historical significance**: These games were written for and tested on the original
  Microsoft BASIC interpreters of the 1970s
- **Feature coverage**: The 96 games collectively exercise nearly every feature of
  8K BASIC including arrays, strings, math functions, and complex control flow
- **Deterministic testing**: With deterministic RND seeding, game outputs are
  reproducible and can be compared exactly
- **Well-documented**: The original book provides context for expected behavior

## Quick Start

### Prerequisites

- Python 3.7 or later
- C compiler (gcc, clang, or MSVC)
- CMake 3.10 or later
- Internet connection (for downloading games)

### Running All Tests

```bash
# From the project root directory
cd compatibility_tests/ahl_games/scripts

# Run all tests (games are auto-downloaded on first run)
python3 ahl_test_runner.py test --all

# Or on Windows
python ahl_test_runner.py test --all
```

### Other Commands

```bash
# Download games only (without testing)
python3 ahl_test_runner.py download

# Force re-download of all games
python3 ahl_test_runner.py download --force

# Test a specific game
python3 ahl_test_runner.py test HAMURABI

# List all games and their status
python3 ahl_test_runner.py list

# List only pending games
python3 ahl_test_runner.py list --status pending

# Show test summary
python3 ahl_test_runner.py status

# Clean up test results only
python3 ahl_test_runner.py clean

# Clean up everything (results AND downloaded games)
python3 ahl_test_runner.py clean --games
```

## How It Works

### Test Architecture

```
compatibility_tests/ahl_games/
├── scripts/
│   ├── ahl_test_runner.py    # Main test runner
│   └── download_games.py     # Game downloader
├── scenarios/                 # Test input files (tracked in git)
│   ├── hamurabi/
│   │   └── scenario_01.input
│   └── ...
├── golden/                    # Expected outputs (tracked in git)
│   ├── hamurabi/
│   │   └── scenario_01.golden
│   └── ...
├── games/                     # Downloaded games (NOT in git)
├── results/                   # Test output (NOT in git)
└── game_registry.json         # Game metadata (tracked in git)
```

### Automatic Game Download

Games are automatically downloaded from:
**https://github.com/coding-horror/basic-computer-games**

This repository (maintained by Jeff Atwood) contains all 96 games from the original
book, preserved in their original BASIC format. We download games on-demand rather
than storing them in our repository to respect the original copyright.

**The download happens automatically** when you run any test command. You don't need
to manually download games - the test runner handles it for you.

### Test Scenarios

Each game has one or more "scenarios" - predefined sequences of inputs that exercise
specific game paths. For example:

**scenarios/hamurabi/scenario_01.input:**
```
100    # Buy 100 acres
20     # Feed 20 bushels
50     # Plant 50 acres
...
```

The test runner feeds this input to the C interpreter and compares the output
against the known-good "golden" output.

### Golden Files

Golden files contain the expected output for each scenario. These were generated
using our verified C interpreter. The comparison:

1. Strips interpreter banners
2. Normalizes whitespace
3. Compares line-by-line

### Deterministic Random Numbers

The key to reproducible tests is deterministic random number generation. Our
interpreter uses the same RND algorithm as original Microsoft BASIC, seeded
identically, producing the exact same sequence every time.

## Directory Structure

| Directory | Contents | Tracked in Git |
|-----------|----------|----------------|
| `scripts/` | Python test runner and downloader | Yes |
| `scenarios/` | Test input files | Yes |
| `golden/` | Expected outputs | Yes |
| `games/` | Downloaded BASIC source | **No** (auto-downloaded) |
| `results/` | Test output files | **No** (auto-generated) |

## Test Results

When tests pass:
```
Testing: HAMURABI
File: .../games/hamurabi.bas
Description: Rule ancient Sumeria for 10 years

  Scenario: scenario_01
    Running C interpreter... done
    Result: PASS

  Summary: 1 passed, 0 failed
```

When tests fail, a diff file is generated showing differences:
```
  Scenario: scenario_01
    Running C interpreter... done
    Result: FAIL
    Diff saved to: results/hamurabi/scenario_01.diff
```

## Adding New Scenarios

To add a test scenario for a game:

1. Create input file: `scenarios/<game>/scenario_XX.input`
2. Generate golden output: `python3 ahl_test_runner.py generate <GAME>`
3. Verify the output looks correct
4. Run tests: `python3 ahl_test_runner.py test <GAME>`

### Input File Format

One input value per line:
```
100
50
YES
NO
```

Comments are NOT supported in input files (they would be sent to the game).

## Cleaning Up

### Clean Results Only
```bash
python3 ahl_test_runner.py clean
```
Removes all files from `results/` but keeps downloaded games.

### Full Clean
```bash
python3 ahl_test_runner.py clean --games
```
Removes both results AND downloaded game files. Games will be re-downloaded
on the next test run.

## Troubleshooting

### Games Won't Download

Check your internet connection. The downloader fetches from GitHub's API which
may rate-limit requests. Wait a minute and try again.

### Test Timeouts

Some games require many inputs or run complex simulations. The default timeout
is 30 seconds. For generating golden files, you can increase it:

```bash
python3 ahl_test_runner.py generate GAME --timeout 60
```

### Interpreter Not Found

The test runner looks for the interpreter at `build/basic8k` (or `build/basic8k.exe`
on Windows). Build it first:

```bash
mkdir build && cd build
cmake ..
make        # or: cmake --build .
```

Or set the path explicitly:
```bash
# Linux/macOS
export BASIC8K_PATH=/path/to/basic8k

# Windows
set BASIC8K_PATH=C:\path\to\basic8k.exe
```

### Tests Fail After Code Changes

If you've modified the interpreter and tests fail:

1. Check the diff file to understand what changed
2. If the new output is correct, regenerate the golden file:
   ```bash
   python3 ahl_test_runner.py generate GAME
   ```

## Cross-Platform Compatibility

The test framework is written in pure Python and works on:

- **Linux**: Tested on Ubuntu, Debian, Fedora
- **macOS**: Tested on macOS 12+
- **Windows**: Tested on Windows 10/11 with Python 3.7+

Platform-specific handling includes:
- Process timeout (killpg on Unix, process groups on Windows)
- Path handling (using pathlib)
- Line ending normalization

## Current Status

| Metric | Count |
|--------|-------|
| Total Games | 96 |
| Tested | 96 |
| Pending | 0 |
| Failed | 0 |

Run `python3 ahl_test_runner.py status` for current status.

## Game Registry

The `game_registry.json` file contains metadata for all 96 games:

```json
{
  "games": {
    "HAMURABI": {
      "number": 43,
      "file": "hamurabi.bas",
      "status": "tested",
      "scenarios_count": 1,
      "description": "Rule ancient Sumeria for 10 years"
    }
  }
}
```

## References

- [BASIC Computer Games](https://en.wikipedia.org/wiki/BASIC_Computer_Games) - Wikipedia
- [coding-horror/basic-computer-games](https://github.com/coding-horror/basic-computer-games) - Game source
- [Altair 8800](https://en.wikipedia.org/wiki/Altair_8800) - The computer these games ran on
- [Microsoft BASIC](https://en.wikipedia.org/wiki/Microsoft_BASIC) - The original interpreter

## License

The test framework code is MIT licensed. The game source files are from the
coding-horror repository and are used for compatibility testing purposes only.
