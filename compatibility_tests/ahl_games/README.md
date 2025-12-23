# David Ahl's BASIC Computer Games - Test Suite

This directory contains a comprehensive testing framework for validating the C17 BASIC interpreter
against David Ahl's "BASIC Computer Games" (1978) - ensuring 100% compatibility with the original
8K BASIC interpreter including random number generation.

## Quick Start

```bash
cd scripts

# List all registered games
./ahl_test_runner.py list

# Test a specific game (C-only mode - uses golden files)
./ahl_test_runner.py test HAMURABI --c-only

# Test a game with fresh SIMH comparison
./ahl_test_runner.py test HAMURABI --simh

# Test all games with scenarios
./ahl_test_runner.py test --all --c-only

# Show test status summary
./ahl_test_runner.py status

# Add a new game
./ahl_test_runner.py add NEWGAME /path/to/newgame.bas
```

## Directory Structure

```
ahl_games/
├── README.md                 # This file
├── game_registry.json        # Master registry of all games and their test status
├── games/                    # BASIC program files (.bas)
│   ├── hamurabi.bas
│   └── ...
├── scenarios/                # Test input scenarios
│   └── hamurabi/
│       ├── scenario_01_instant_death.input
│       ├── scenario_02_early_death.input
│       └── ...
├── golden/                   # Expected outputs from original 8K BASIC
│   └── hamurabi/
│       ├── scenario_01_instant_death.golden
│       └── ...
├── results/                  # Test results (gitignored)
│   └── hamurabi/
│       ├── scenario_01_instant_death.c_output
│       ├── scenario_01_instant_death.diff
│       └── ...
└── scripts/
    ├── ahl_test_runner.py    # Main test runner
    └── run_simh_game.exp     # Expect script for SIMH
```

## Game Registry (game_registry.json)

The registry tracks all games from David Ahl's book with their test status:

```json
{
  "games": {
    "HAMURABI": {
      "file": "hamurabi.bas",
      "status": "tested",           // "tested", "pending", or "failed"
      "scenarios_count": 10,        // Number of test scenarios
      "description": "Rule ancient Sumeria for 10 years...",
      "uses_rnd": true,             // Uses RND function
      "uses_input": true,           // Uses INPUT statement
      "features": ["GOSUB/RETURN", "FOR/NEXT", "TAB"],
      "notes": "Complex game with multiple endings"
    }
  }
}
```

## Test Scenarios

Each scenario is an `.input` file containing one input value per line:

```
# scenarios/hamurabi/scenario_01_instant_death.input
0      # Buy 0 acres
0      # Sell 0 acres
0      # Feed 0 people (causes instant death)
0      # Plant 0 acres
```

### Naming Convention

`scenario_XX_description.input` where:
- `XX` is a two-digit number (01, 02, ...)
- `description` briefly describes the test case

### Coverage Goals

For each game, create scenarios covering:
1. **Happy path**: Normal gameplay to successful completion
2. **Failure paths**: Ways to lose/die/fail
3. **Edge cases**: Boundary inputs, zero values, large numbers
4. **All branches**: Exercise different game outcomes (win/lose/draw)
5. **Input validation**: Invalid inputs that the game handles

## Golden Files

Golden files contain the expected output from the original 8K BASIC interpreter running via SIMH.

### Generating Golden Files

```bash
# Generate golden output for a specific game
./ahl_test_runner.py generate GAMENAME

# Or run test with --simh to generate missing golden files
./ahl_test_runner.py test GAMENAME --simh
```

### Golden File Format

- Captured from SIMH (original 8080 BASIC)
- Banner stripped (starts with actual game output)
- Trailing whitespace normalized
- Bell characters (CHR$(7)) removed

## Testing Modes

### C-Only Mode (Fast)
```bash
./ahl_test_runner.py test GAMENAME --c-only
```
- Tests C interpreter against existing golden files
- Skips scenarios without golden files
- Fast - no SIMH required

### SIMH Comparison Mode
```bash
./ahl_test_runner.py test GAMENAME --simh
```
- Runs both C and SIMH interpreters
- Compares outputs directly
- Slower but validates against original
- Generates golden files if missing

## Adding New Games

### 1. Add Game File
```bash
./ahl_test_runner.py add GAMENAME /path/to/game.bas
```

Or manually:
1. Copy `.bas` file to `games/`
2. Add entry to `game_registry.json`

### 2. Create Scenarios
```bash
mkdir -p scenarios/gamename/
```

Create input files for different test cases.

### 3. Generate Golden Files
```bash
./ahl_test_runner.py generate GAMENAME
```

### 4. Run Tests
```bash
./ahl_test_runner.py test GAMENAME --c-only
```

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `BASIC8K_PATH` | `../../build/basic8k` | Path to C interpreter |
| `SIMH_DIR` | `/Users/tb/dev/NEW-BASIC/mbasic2025/4k8k/8k` | SIMH directory |

## Troubleshooting

### Test Failures

1. Check the diff file in `results/gamename/scenario.diff`
2. Compare C output vs golden file
3. Common issues:
   - Banner stripping differences
   - Floating-point precision
   - Whitespace differences

### SIMH Timeouts

Some complex scenarios may timeout in SIMH. Solutions:
1. Use simpler input sequences
2. Increase timeout in expect script
3. Use C-only mode with manually verified golden files

### Missing Golden Files

```bash
# Generate from SIMH
./ahl_test_runner.py generate GAMENAME

# Or verify C output manually and copy to golden
cp results/gamename/scenario.c_output golden/gamename/scenario.golden
```

## Test Statistics

Current test coverage:
- **Total Games**: 77 from the original book
- **Tested**: Games with passing scenarios
- **Pending**: Games without scenarios yet
- **Failed**: Games with failing tests (interpreter bugs)

Run `./ahl_test_runner.py status` for current statistics.

## Games List

See `./ahl_test_runner.py list` for the full list of games and their status.

Key games from the book:
- **HAMURABI** - Resource management simulation (tested, 10 scenarios)
- **SUPER STAR TREK** - Space combat (complex, uses DEF FN)
- **ACEY DUCEY** - Card game
- **BLACKJACK** - Casino card game
- **HANGMAN** - Word guessing
- **TIC-TAC-TOE** - Board game
- And 70+ more...

## Contributing Test Scenarios

When adding scenarios:
1. Name files descriptively
2. Document what the scenario tests
3. Cover win/lose/error paths
4. Test edge cases
5. Verify with SIMH first if possible
