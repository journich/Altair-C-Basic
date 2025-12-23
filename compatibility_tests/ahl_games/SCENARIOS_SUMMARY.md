# Action/Shooting Games - Test Scenarios Summary

## Overview
Created comprehensive test scenarios for 7 action/shooting games from David Ahl's BASIC Computer Games collection. These scenarios test game mechanics, physics calculations, and player feedback systems.

## Games Processed

### 1. GUNNER (Artillery Targeting)
**Location:** `/Users/tb/dev/Altair-C-Basic/compatibility_tests/ahl_games/scenarios/gunner/`
**Scenarios:** 6

- `scenario_01_first_shot_hit.input` - Tests quick hit scenario
- `scenario_02_adjust_from_over.input` - Tests adjustment from overshooting
- `scenario_03_adjust_from_under.input` - Tests adjustment from undershooting
- `scenario_04_boundary_test.input` - Tests boundary conditions (0° and 90°)
- `scenario_05_enemy_destroys.input` - Tests losing condition (6 missed shots)
- `scenario_06_multiple_targets.input` - Tests multiple target engagement

**Mechanics Tested:**
- Ballistic trajectory calculation using SIN function
- Over/under feedback system
- Boundary validation (1-89 degrees)
- Enemy counter-attack after 5 missed shots

### 2. BOMBARDMENT (Grid Combat)
**Location:** `/Users/tb/dev/Altair-C-Basic/compatibility_tests/ahl_games/scenarios/bombardment/`
**Scenarios:** 4

- `scenario_01_quick_win.input` - Tests winning quickly
- `scenario_02_systematic_search.input` - Tests systematic grid searching
- `scenario_03_loss_game.input` - Tests losing scenario
- `scenario_04_corner_positions.input` - Tests corner strategy

**Mechanics Tested:**
- 25-position grid system
- Platoon placement validation (no duplicates)
- Turn-based missile exchange
- Win/loss conditions (destroy all 4 enemy platoons)

### 3. DEPTHCHARGE (Submarine Hunting)
**Location:** `/Users/tb/dev/Altair-C-Basic/compatibility_tests/ahl_games/scenarios/depthcharge/`
**Scenarios:** 5

- `scenario_01_direct_hit.input` - Tests direct hit on first try
- `scenario_02_binary_search.input` - Tests binary search strategy
- `scenario_03_corner_hunt.input` - Tests corner coordinates
- `scenario_04_large_grid.input` - Tests larger search space (20x20x20)
- `scenario_05_out_of_shots.input` - Tests running out of depth charges

**Mechanics Tested:**
- 3D coordinate system
- Manhattan distance checking
- Directional feedback (NORTH/SOUTH/EAST/WEST/HIGH/LOW)
- Shot count calculation: N = INT(LOG(G)/LOG(2)) + 1

### 4. TARGET (3D Space Combat)
**Location:** `/Users/tb/dev/Altair-C-Basic/compatibility_tests/ahl_games/scenarios/target/`
**Scenarios:** 5

- `scenario_01_first_shot_hit.input` - Tests accurate first shot
- `scenario_02_adjust_angle.input` - Tests angle adjustment
- `scenario_03_adjust_distance.input` - Tests distance adjustment
- `scenario_04_self_destruct.input` - Tests self-destruct (<20km)
- `scenario_05_precision_shots.input` - Tests precision targeting

**Mechanics Tested:**
- 3D spherical coordinates (angles from X and Z axes)
- Distance calculation using Euclidean formula
- Hit detection (< 20 kilometers)
- Position feedback (front/behind, left/right, above/below)

### 5. ORBIT (Orbital Mechanics)
**Location:** `/Users/tb/dev/Altair-C-Basic/compatibility_tests/ahl_games/scenarios/orbit/`
**Scenarios:** 5

- `scenario_01_lucky_hit.input` - Tests lucky shot
- `scenario_02_tracking_target.input` - Tests tracking moving target
- `scenario_03_bracketing.input` - Tests distance bracketing
- `scenario_04_timeout.input` - Tests 7-hour timeout
- `scenario_05_perfect_intercept.input` - Tests perfect interception

**Mechanics Tested:**
- Polar orbit simulation
- Angular position updates (A = A + R per hour)
- Circular angle wrapping (>360° → -360°)
- Distance calculation: C = SQR(D² + D1² - 2*D*D1*COS(T))
- Hit detection (< 50 units = 5000 miles)

### 6. SPLAT (Parachute Timing)
**Location:** `/Users/tb/dev/Altair-C-Basic/compatibility_tests/ahl_games/scenarios/splat/`
**Scenarios:** 6

- `scenario_01_splat_too_long.input` - Tests waiting too long (splat)
- `scenario_02_safe_early.input` - Tests opening too early (safe but low score)
- `scenario_03_earth_custom.input` - Tests custom Earth parameters
- `scenario_04_moon_jump.input` - Tests low-gravity moon jump
- `scenario_05_multiple_jumps.input` - Tests multiple jumps with scoring
- `scenario_06_jupiter_heavy.input` - Tests high-gravity jump

**Mechanics Tested:**
- Random altitude generation (1000-10000 ft)
- Terminal velocity physics
- Acceleration calculations (various planets)
- Freefall physics: D = D1 - (A/2)*I²
- Terminal velocity: D = D1 - (V²/(2*A)) - (V*(I-(V/A)))
- Ranking system (42 best jumps tracked)

### 7. LUNAR (Lunar Landing)
**Location:** `/Users/tb/dev/Altair-C-Basic/compatibility_tests/ahl_games/scenarios/lunar/`
**Scenarios:** 7

- `scenario_01_perfect_landing.input` - Tests perfect landing (≤1.2 MPH)
- `scenario_02_good_landing.input` - Tests good landing (≤10 MPH)
- `scenario_03_crash.input` - Tests free-fall crash
- `scenario_04_fuel_runout.input` - Tests running out of fuel
- `scenario_05_conservative.input` - Tests conservative approach
- `scenario_06_aggressive.input` - Tests aggressive burn strategy
- `scenario_07_damaged_landing.input` - Tests damaged landing (10-60 MPH)

**Mechanics Tested:**
- Altitude tracking (starts at 120 miles)
- Velocity physics (starts at 1 mi/hr downward)
- Fuel management (16,500 lbs fuel, 32,500 lbs total mass)
- Burn rate control (0-200 lbs/sec)
- Physics simulation with gravity constant G=0.001
- Landing velocity thresholds:
  - ≤1.2 MPH: Perfect
  - ≤10 MPH: Good
  - ≤60 MPH: Damaged
  - >60 MPH: Destroyed

## Test Approach

### Scenario Design Philosophy
1. **Boundary Testing** - Test edge cases (min/max values, zero input)
2. **Feedback Loop Testing** - Verify game provides correct directional feedback
3. **Win Conditions** - Test various ways to win
4. **Loss Conditions** - Test various ways to lose
5. **Physics Accuracy** - Verify mathematical calculations
6. **State Management** - Test multi-round games

### Input Format
All scenario files use simple line-based input:
- Each line represents one user input
- Blank lines are preserved where needed for multi-value inputs
- Files end with final answer to "play again" prompt

### Random Number Handling
These games use RND() for:
- Target positions (GUNNER, DEPTHCHARGE, TARGET, BOMBARDMENT)
- Initial conditions (ORBIT, SPLAT altitude, LUNAR may have variations)

**Important:** Since RND() values cannot be predicted without seeding, golden files will be specific to the C interpreter's RND implementation. Scenarios are designed to work with the generated random values.

## Statistics
- **Total Games:** 7
- **Total Scenarios:** 38
- **Total Input Files:** 38
- **Average Scenarios per Game:** 5.4

## Game Mechanics Summary

| Game | Type | Physics | Dimensions | Win Condition |
|------|------|---------|------------|---------------|
| GUNNER | Artillery | Ballistics | 1D (elevation) | Hit within 100 yards |
| BOMBARDMENT | Grid Combat | None | 2D (grid 1-25) | Destroy 4 enemy platoons |
| DEPTHCHARGE | Search | Manhattan | 3D (cubic) | Find submarine |
| TARGET | Shooting | Euclidean | 3D (spherical) | Hit within 20 km |
| ORBIT | Intercept | Circular | 2D (polar) | Hit within 5000 miles |
| SPLAT | Timing | Gravity | 1D (altitude) | Lowest safe opening |
| LUNAR | Landing | Rocket | 1D (altitude) | Land ≤1.2 MPH |

## Next Steps

To generate golden reference files:
```bash
cd /Users/tb/dev/Altair-C-Basic/compatibility_tests/ahl_games
python3 scripts/ahl_test_runner.py generate GUNNER
python3 scripts/ahl_test_runner.py generate BOMBARDMENT
python3 scripts/ahl_test_runner.py generate DEPTHCHARGE
python3 scripts/ahl_test_runner.py generate TARGET
python3 scripts/ahl_test_runner.py generate ORBIT
python3 scripts/ahl_test_runner.py generate SPLAT
python3 scripts/ahl_test_runner.py generate LUNAR
```

Or generate all at once:
```bash
for game in GUNNER BOMBARDMENT DEPTHCHARGE TARGET ORBIT SPLAT LUNAR; do
  python3 scripts/ahl_test_runner.py generate $game
done
```

To test against C interpreter:
```bash
python3 scripts/ahl_test_runner.py test GUNNER --c-only
# Or test all:
python3 scripts/ahl_test_runner.py test --all --c-only
```

## Files Created

### Scenario Input Files (38 total)
```
scenarios/
├── gunner/ (6 scenarios)
├── bombardment/ (4 scenarios)
├── depthcharge/ (5 scenarios)
├── target/ (5 scenarios)
├── orbit/ (5 scenarios)
├── splat/ (6 scenarios)
└── lunar/ (7 scenarios)
```

### Helper Scripts
```
scripts/
└── generate_golden_simple.py - Simple golden file generator
```

## Game-Specific Notes

### GUNNER
- Max 5 shots per target before enemy destroys you
- 4 targets total in a session
- Excellent shooting = ≤18 total rounds

### BOMBARDMENT  
- Computer uses anti-repeat logic (stores in M() array)
- Player can win by systematic search or luck

### DEPTHCHARGE
- Shot count formula ensures sufficient tries for binary search
- Optimal strategy: binary search in each dimension

### TARGET
- Continuously generates new targets after each hit
- No game over - infinite targeting practice
- Tracks shot count per target

### ORBIT
- Ship moves 10-30 degrees per hour counterclockwise
- Starting position and rate are random
- Must calculate lead angle for moving target

### SPLAT
- Ranks jumps against previous 42 attempts
- Random planet selection adds variety
- Terminal velocity affects fall distance differently

### LUNAR
- Non-linear physics simulation
- Multiple subroutines for different flight phases
- Fuel management is critical

