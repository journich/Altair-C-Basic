# Sports Games Test Scenarios Summary

This document summarizes the test scenarios created for the AHL sports games.

## Successfully Tested Games (with Golden Files)

### 1. BASKETBALL (basketball.bas)
A basketball simulation where you play as Dartmouth College against an opponent.

**Scenarios Created:**
- `scenario_01_close_game.input/.golden` - Competitive game using zone defense (7) and primarily layup shots (3). Shows a close match with varied outcomes.
- `scenario_02_varied_strategy.input/.golden` - Uses man-to-man defense (6.5) with mixed shot selection (layups, short jumps, long jumps) to demonstrate strategic variety.
- `scenario_03_aggressive_press.input/.golden` - Employs press defense (6) with consistent layup attempts, showing aggressive play style.

**Key Inputs:**
- Defense: 6 (press), 6.5 (man-to-man), 7 (zone), 7.5 (none)
- Shot types: 1 (long jump), 2 (short jump), 3 (layup), 4 (set shot)
- Shot 0 allows changing defense mid-game

**Test Strategy:**
The RND sequence determines shot success, rebounds, and fouls. Layups (option 3) tend to be high-percentage shots. Games run for 100 time units (50 per half). Each scenario provides enough inputs to cover typical game flow including offensive and defensive possessions.

### 2. BOWLING (bowling.bas)
A ten-frame bowling simulation supporting up to 4 players.

**Scenarios Created:**
- `scenario_01_single_player.input/.golden` - Single player (FRANK) bowling a complete 10-frame game. Demonstrates basic gameplay mechanics.
- `scenario_02_two_players.input/.golden` - Two players (ALICE and BOB) competing. Shows multi-player scoring and frame tracking.

**Key Inputs:**
- Answer "N" to skip instructions
- Number of players (1-4)
- Player names
- Type "ROLL" for each ball (2 per frame, or 3 in 10th frame if spare/strike)
- Answer "N" when asked to play again

**Test Strategy:**
Each roll is randomly generated - the game uses RND to determine pins knocked down in a realistic pattern. Strikes, spares, and scoring are automatically calculated. The scenarios provide 20 "ROLL" commands for single player (covering all possible balls) and 40 for two players.

### 3. BOXING (boxing.bas)
Olympic-style boxing simulation (3 rounds, 2 out of 3 wins).

**Scenarios Created:**
- `scenario_01_uppercut_master.input/.golden` - APOLLO vs ROCKY, with best punch=3 (uppercut), vulnerability=1 (full swing). Strategy focuses on uppercut attacks.
- `scenario_02_mixed_strategy.input/.golden` - TYSON vs HOLYFIELD, with best=2 (hook), vulnerability=3 (uppercut). Uses varied punch combinations.
- `scenario_03_jabber.input/.golden` - ALI vs FRAZIER, with best=4 (jab), vulnerability=2 (hook). Emphasizes jab-based strategy.

**Key Inputs:**
- Opponent name
- Your fighter name
- Best punch (1=full swing, 2=hook, 3=uppercut, 4=jab)
- Vulnerability (1-4, same options)
- Punch selection during each exchange (7 per round × 3 rounds = 21 inputs)

**Test Strategy:**
The RND sequence determines punch success, blocks, and damage. Using your best punch increases effectiveness. Avoiding your vulnerability reduces opponent damage. Computer opponent has random best/vulnerability. Each round has ~7 exchanges, needing about 21 punch inputs total.

### 4. SLALOM (slalom.bas)
Ski slalom racing simulation with gate navigation and speed control.

**Scenarios Created:**
- `scenario_01_simple_run.input/.golden` - 5-gate course with beginner skill (2), using safe speed control (option 4).
- `scenario_02_expert_run.input/.golden` - 10-gate course with expert skill (3), using varied speed control for optimal times.
- `scenario_03_conservative_run.input/.golden` - 8-gate course with beginner skill (1), maintaining constant speed throughout.

**Key Inputs:**
- Number of gates (1-25)
- Command: "RUN" to start race
- Skill level: 1 (worst) to 3 (best)
- Options for each gate:
  - 1 = speed up a lot
  - 2 = speed up a little
  - 3 = speed up a teensy
  - 4 = maintain speed
  - 5 = slow down a teensy
  - 6 = slow down a little
  - 7 = slow down a lot
  - 8 = try to cheat
  - 0 = check current time
- Answer "NO" when asked to race again

**Test Strategy:**
Each gate has a maximum safe speed (DATA at line 1810). Speed is randomized at start. Option 4 (maintain) is safest. Going too fast risks wiping out or snagging flags. Skill level affects scoring thresholds for medals. Higher-skilled players can achieve faster times. The RND sequence affects speed changes and penalties.

## Games with Input Scenarios Only (No Golden Files)

The following games had test scenarios created but golden files were not generated due to their complexity and the difficulty in creating inputs that lead to clean game completion:

### 5. GOLF (golf.bas)
- Complex club selection system (clubs 1-4, 12-29 with transformations)
- Distance, putting, and hazard mechanics
- Input files created but require more testing for complete 18-hole rounds

### 6. HOCKEY (hockey.bas)
- Detailed hockey simulation with line management
- Complex passing and shooting mechanics
- Input files created but require extensive inputs for full game

### 7. HORSERACE (horserace.bas)
- Horse betting simulation with odds calculation
- Requires comma-separated input format (horse#,amount)
- Input files created but need format refinement

## Summary Statistics

**Games with Complete Test Coverage:**
- Basketball: 3 scenarios (3 input files, 3 golden files)
- Bowling: 2 scenarios (2 input files, 2 golden files)
- Boxing: 3 scenarios (3 input files, 3 golden files)
- Slalom: 3 scenarios (3 input files, 3 golden files)

**Total:** 11 complete test scenarios with golden files

**Games with Partial Coverage:**
- Golf: 3 input files
- Hockey: 2 input files
- Horserace: 3 input files

**Total:** 8 input-only test scenarios

## Testing Methodology

1. **Source Code Analysis:** Each game was analyzed to understand input requirements, game flow, and RND-dependent outcomes.

2. **Input Sequence Design:** Test inputs were crafted to:
   - Exercise different game strategies
   - Cover various input paths
   - Generate deterministic output with the fixed RND sequence
   - Complete games cleanly without hanging or looping

3. **Golden File Generation:** Successfully completed games had their output captured as golden files for regression testing.

4. **Validation:** Each scenario was tested with timeout protection to prevent infinite loops.

## File Locations

All scenario files are located in:
```
/Users/tb/dev/Altair-C-Basic/compatibility_tests/ahl_games/scenarios/
├── basketball/
│   ├── scenario_01_close_game.{input,golden}
│   ├── scenario_02_varied_strategy.{input,golden}
│   └── scenario_03_aggressive_press.{input,golden}
├── bowling/
│   ├── scenario_01_single_player.{input,golden}
│   └── scenario_02_two_players.{input,golden}
├── boxing/
│   ├── scenario_01_uppercut_master.{input,golden}
│   ├── scenario_02_mixed_strategy.{input,golden}
│   └── scenario_03_jabber.{input,golden}
└── slalom/
    ├── scenario_01_simple_run.{input,golden}
    ├── scenario_02_expert_run.{input,golden}
    └── scenario_03_conservative_run.{input,golden}
```

## Usage

To test a scenario:
```bash
/Users/tb/dev/Altair-C-Basic/build/basic8k games/<game>.bas < scenarios/<game>/<scenario>.input
```

To verify against golden file:
```bash
/Users/tb/dev/Altair-C-Basic/build/basic8k games/<game>.bas < scenarios/<game>/<scenario>.input > output.txt
diff output.txt scenarios/<game>/<scenario>.golden
```

## Notes

- All tests use the deterministic RND sequence from the C BASIC interpreter
- Basketball games are the most complex, requiring ~50+ inputs for full game
- Bowling is the most straightforward with predictable input/output
- Boxing provides good variety with different fighting strategies
- Slalom demonstrates speed/risk tradeoff mechanics

## Future Work

For the games with input-only scenarios (golf, hockey, horserace):
1. Golf: Refine club selection logic and create shorter test courses
2. Hockey: Simplify to 1-2 minute games instead of full periods
3. Horserace: Confirm INPUT format requirements for betting
