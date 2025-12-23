#!/usr/bin/env python3
"""
Simple golden file generator for action games.
Since these games use RND(), we can't predict exact values.
This script just runs the games and captures output for reference.
"""

import subprocess
import sys
from pathlib import Path

# Configuration
AHL_DIR = Path(__file__).parent.parent
BASIC8K = AHL_DIR.parent.parent / "build" / "basic8k"
SCENARIOS_DIR = AHL_DIR / "scenarios"
GOLDEN_DIR = AHL_DIR / "golden"
GAMES_DIR = AHL_DIR / "games"

def strip_banner(output):
    """Remove interpreter banner."""
    lines = output.split('\n')
    result = []
    skip = True

    for line in lines:
        if skip:
            if 'MICROSOFT BASIC' in line or 'ALTAIR' in line or 'COPYRIGHT' in line:
                continue
            if 'BYTES FREE' in line or line.strip() == 'OK' or '[8K VERSION]' in line or '[4K VERSION]' in line:
                continue
            if line.strip() == '':
                continue
            skip = False

        if not skip:
            result.append(line.rstrip())

    # Remove trailing empty lines
    while result and not result[-1]:
        result.pop()

    return '\n'.join(result)

def generate_golden(game_name):
    """Generate golden files for a game."""
    game_file = GAMES_DIR / f"{game_name.lower()}.bas"
    if not game_file.exists():
        print(f"ERROR: Game file not found: {game_file}")
        return False

    scenario_dir = SCENARIOS_DIR / game_name.lower()
    if not scenario_dir.exists():
        print(f"ERROR: No scenarios found for {game_name}")
        return False

    golden_dir = GOLDEN_DIR / game_name.lower()
    golden_dir.mkdir(parents=True, exist_ok=True)

    print(f"Generating golden files for {game_name}...")

    for scenario_file in sorted(scenario_dir.glob("*.input")):
        scenario_name = scenario_file.stem
        print(f"  {scenario_name}...", end=" ", flush=True)

        try:
            with open(scenario_file, 'r') as f:
                input_data = f.read()

            result = subprocess.run(
                [str(BASIC8K), str(game_file)],
                input=input_data,
                capture_output=True,
                text=True,
                timeout=10
            )

            output = result.stdout + result.stderr
            cleaned = strip_banner(output)

            golden_file = golden_dir / f"{scenario_name}.golden"
            with open(golden_file, 'w') as f:
                f.write(cleaned)

            print("done")

        except subprocess.TimeoutExpired:
            print("TIMEOUT")
            # Write timeout indicator
            golden_file = golden_dir / f"{scenario_name}.golden"
            with open(golden_file, 'w') as f:
                f.write("TIMEOUT - game did not complete\n")

        except Exception as e:
            print(f"ERROR: {e}")
            return False

    return True

if __name__ == "__main__":
    games = ["gunner", "bombardment", "depthcharge", "target", "orbit", "splat", "lunar"]

    for game in games:
        if not generate_golden(game):
            sys.exit(1)

    print("\nAll golden files generated successfully!")
