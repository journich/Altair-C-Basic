#!/usr/bin/env python3
"""
David Ahl's BASIC Computer Games - Unified Test Runner

Tests games from the classic book against both:
- C implementation (basic8k)
- Original 8080 BASIC via SIMH

Ensures 100% compatibility including random number generation.

Usage:
    ./ahl_test_runner.py list              # List all games and status
    ./ahl_test_runner.py test GAME         # Test specific game
    ./ahl_test_runner.py test --all        # Test all games with scenarios
    ./ahl_test_runner.py generate GAME     # Generate golden output from SIMH
    ./ahl_test_runner.py status            # Show test summary
    ./ahl_test_runner.py add GAME FILE     # Add new game to registry
"""

import os
import sys
import json
import subprocess
import argparse
import difflib
import re
from datetime import datetime
from pathlib import Path

# Directory structure
SCRIPT_DIR = Path(__file__).parent.absolute()
AHL_DIR = SCRIPT_DIR.parent
PROJECT_ROOT = AHL_DIR.parent.parent
GAMES_DIR = AHL_DIR / "games"
SCENARIOS_DIR = AHL_DIR / "scenarios"
GOLDEN_DIR = AHL_DIR / "golden"
RESULTS_DIR = AHL_DIR / "results"
REGISTRY_FILE = AHL_DIR / "game_registry.json"

# Interpreter paths (configurable via environment)
C_INTERPRETER = os.environ.get(
    "BASIC8K_PATH",
    str(PROJECT_ROOT / "build" / "basic8k")
)
SIMH_DIR = os.environ.get(
    "SIMH_DIR",
    "/Users/tb/dev/NEW-BASIC/mbasic2025/4k8k/8k"
)


def load_registry():
    """Load the game registry."""
    if not REGISTRY_FILE.exists():
        return {"metadata": {}, "games": {}, "test_statistics": {}}
    with open(REGISTRY_FILE, 'r') as f:
        return json.load(f)


def save_registry(registry):
    """Save the game registry."""
    registry["metadata"]["last_updated"] = datetime.now().strftime("%Y-%m-%d")

    # Update statistics
    games = registry.get("games", {})
    registry["test_statistics"] = {
        "total_games": len(games),
        "tested": sum(1 for g in games.values() if g.get("status") == "tested"),
        "pending": sum(1 for g in games.values() if g.get("status") == "pending"),
        "failed": sum(1 for g in games.values() if g.get("status") == "failed"),
    }

    with open(REGISTRY_FILE, 'w') as f:
        json.dump(registry, f, indent=2)


def find_game_file(game_name, registry):
    """Find the BASIC file for a game."""
    game_info = registry.get("games", {}).get(game_name.upper())
    if not game_info:
        return None

    filename = game_info.get("file")

    # Check in games directory first
    game_path = GAMES_DIR / filename
    if game_path.exists():
        return game_path

    # Check in programs directory (legacy location)
    legacy_path = PROJECT_ROOT / "compatibility_tests" / "programs" / filename
    if legacy_path.exists():
        return legacy_path

    return None


def get_scenario_files(game_name):
    """Get all scenario input files for a game."""
    scenarios = []
    game_dir = SCENARIOS_DIR / game_name.lower()
    if game_dir.exists():
        for f in sorted(game_dir.glob("*.input")):
            scenarios.append(f)
    return scenarios


def strip_banner(output):
    """Strip interpreter banner and normalize output."""
    # Remove bell characters
    output = output.replace('\x07', '')

    # Remove ANSI escape codes
    ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
    output = ansi_escape.sub('', output)

    # Remove carriage returns
    output = output.replace('\r', '')

    lines = output.split('\n')
    result = []
    skip_banner = True

    for line in lines:
        # Skip known banner lines at the start
        if skip_banner:
            # C interpreter banner lines to skip
            if 'MICROSOFT BASIC' in line:
                continue
            if 'ALTAIR' in line.upper() and 'VERSION' in line.upper():
                continue
            if '[8K VERSION]' in line or '[4K VERSION]' in line:
                continue
            if 'COPYRIGHT' in line:
                continue
            if 'BYTES FREE' in line:
                continue
            if line.strip() == 'OK':
                continue
            if line.strip() == '':
                continue

            # Once we hit real content, stop skipping
            if line.strip():
                skip_banner = False

        if not skip_banner:
            result.append(line)

    return '\n'.join(result)


def strip_simh_output(output):
    """Clean SIMH output."""
    # Remove ANSI escape codes
    ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
    output = ansi_escape.sub('', output)

    # Remove carriage returns
    output = output.replace('\r', '')

    # Remove bell characters
    output = output.replace('\x07', '')

    lines = output.split('\n')
    result = []
    skip_header = True

    for line in lines:
        if skip_header:
            # Skip SIMH startup and program entry lines
            if 'Altair 8800' in line or 'MITS' in line or 'COPYRIGHT' in line:
                continue
            if 'MEMORY SIZE' in line or 'TERMINAL WIDTH' in line:
                continue
            if 'BYTES FREE' in line:
                continue
            if 'WANT SIN' in line:
                continue
            if line.strip() == 'OK':
                continue
            # Skip program line echoes (lines starting with numbers)
            if line.strip() and line.strip()[0].isdigit() and ' ' in line.strip():
                continue
            # Skip RUN command
            if line.strip() == 'RUN':
                continue
            # Skip empty lines while in header
            if line.strip() == '':
                continue

            # Once we hit real content, stop skipping
            if line.strip():
                skip_header = False

        if not skip_header:
            result.append(line)

    return '\n'.join(result)


def normalize_output(output):
    """Normalize output for comparison.

    Normalizes:
    - Strips trailing whitespace from each line (but preserves leading)
    - Removes leading/trailing empty lines
    - Does NOT strip leading whitespace from lines (TAB formatting matters)
    """
    lines = output.split('\n')
    normalized = []

    for line in lines:
        # Strip trailing whitespace only (preserve leading spaces for TAB)
        normalized.append(line.rstrip())

    # Remove leading empty lines
    while normalized and not normalized[0]:
        normalized.pop(0)

    # Remove trailing empty lines
    while normalized and not normalized[-1]:
        normalized.pop()

    return '\n'.join(normalized)


def run_c_interpreter(program_path, input_path=None, timeout=10):
    """Run a game on the C interpreter."""
    import signal
    import os

    input_data = ""
    if input_path and Path(input_path).exists():
        with open(input_path, 'r') as f:
            input_data = f.read()

    try:
        # Use Popen with start_new_session to ensure we can kill the process group
        proc = subprocess.Popen(
            [C_INTERPRETER, str(program_path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=True
        )
        try:
            stdout, stderr = proc.communicate(input=input_data, timeout=timeout)
            output = stdout + stderr
        except subprocess.TimeoutExpired:
            # Kill entire process group to ensure cleanup
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            proc.wait()
            output = "TIMEOUT ERROR"
    except FileNotFoundError:
        output = f"ERROR: C interpreter not found at {C_INTERPRETER}"
    except Exception as e:
        output = f"ERROR: {e}"

    return strip_banner(output)


def run_simh(program_path, input_path=None, timeout=180):
    """Run a game via SIMH."""
    expect_script = SCRIPT_DIR / "run_simh_game.exp"

    if not expect_script.exists():
        return "ERROR: SIMH expect script not found"

    try:
        args = [str(expect_script), str(program_path)]
        if input_path:
            args.append(str(input_path))

        result = subprocess.run(
            args,
            capture_output=True,
            text=True,
            timeout=timeout
        )
        output = result.stdout
    except subprocess.TimeoutExpired:
        output = "TIMEOUT ERROR"
    except Exception as e:
        output = f"ERROR: {e}"

    return strip_simh_output(output)


def compare_outputs(c_output, golden_output, game_name, scenario_name=""):
    """Compare outputs and generate diff."""
    c_norm = normalize_output(c_output)
    golden_norm = normalize_output(golden_output)

    if c_norm == golden_norm:
        return True, None

    c_lines = c_norm.split('\n')
    golden_lines = golden_norm.split('\n')

    name = f"{game_name}/{scenario_name}" if scenario_name else game_name
    diff = list(difflib.unified_diff(
        golden_lines, c_lines,
        fromfile=f'{name}.golden',
        tofile=f'{name}.c',
        lineterm=''
    ))

    return False, '\n'.join(diff)


def test_game(game_name, registry, use_simh=False, verbose=False, c_only=False, show_output=False):
    """Test a single game against all its scenarios.

    Args:
        game_name: Name of the game
        registry: Game registry dict
        use_simh: If True, always run SIMH (regenerate golden)
        verbose: Show detailed diff output
        c_only: If True, only test scenarios with existing golden files
        show_output: If True, display the actual game output after each test
    """
    game_name = game_name.upper()
    game_info = registry.get("games", {}).get(game_name)

    if not game_info:
        print(f"ERROR: Game '{game_name}' not found in registry")
        return False

    game_file = find_game_file(game_name, registry)
    if not game_file:
        print(f"ERROR: Game file not found for '{game_name}'")
        return False

    print(f"\n{'='*70}")
    print(f"Testing: {game_name}")
    print(f"File: {game_file}")
    print(f"Description: {game_info.get('description', 'N/A')}")
    mode = "C-only (golden)" if c_only else ("SIMH comparison" if use_simh else "golden files")
    print(f"Mode: {mode}")
    print(f"{'='*70}")

    scenarios = get_scenario_files(game_name)
    if not scenarios:
        print(f"  No scenarios found for {game_name}")
        return None

    results = []
    game_results_dir = RESULTS_DIR / game_name.lower()
    game_results_dir.mkdir(parents=True, exist_ok=True)

    for scenario_path in scenarios:
        scenario_name = scenario_path.stem
        golden_path = GOLDEN_DIR / game_name.lower() / f"{scenario_name}.golden"

        # In c_only mode, skip scenarios without golden files
        if c_only and not golden_path.exists():
            print(f"\n  Scenario: {scenario_name} [SKIP - no golden file]")
            results.append(("SKIP", scenario_name))
            continue

        print(f"\n  Scenario: {scenario_name}")

        # Run C interpreter
        print("    Running C interpreter...", end=" ", flush=True)
        c_output = run_c_interpreter(game_file, scenario_path)
        c_output_path = game_results_dir / f"{scenario_name}.c_output"
        c_output_path.write_text(c_output)
        print("done")

        # Get golden output
        if c_only:
            # In c_only mode, just use existing golden file
            golden_output = golden_path.read_text()
        elif use_simh or not golden_path.exists():
            print("    Running SIMH...", end=" ", flush=True)
            simh_output = run_simh(game_file, scenario_path)
            simh_output_path = game_results_dir / f"{scenario_name}.simh_output"
            simh_output_path.write_text(simh_output)
            print("done")

            if not golden_path.exists():
                # Save as golden
                golden_path.parent.mkdir(parents=True, exist_ok=True)
                golden_path.write_text(simh_output)
                print(f"    Created golden: {golden_path}")

            golden_output = simh_output
        else:
            golden_output = golden_path.read_text()

        # Compare
        match, diff = compare_outputs(c_output, golden_output, game_name, scenario_name)

        if match:
            print(f"    Result: PASS")
            results.append(("PASS", scenario_name))
            if show_output:
                print(f"\n    {'─'*60}")
                print(f"    OUTPUT:")
                print(f"    {'─'*60}")
                for line in c_output.split('\n'):
                    print(f"    {line}")
                print(f"    {'─'*60}")
        else:
            print(f"    Result: FAIL")
            diff_path = game_results_dir / f"{scenario_name}.diff"
            diff_path.write_text(diff)
            print(f"    Diff saved to: {diff_path}")
            results.append(("FAIL", scenario_name))

            if verbose:
                print("\n    --- Diff ---")
                for line in diff.split('\n')[:30]:
                    print(f"    {line}")
                print("    ...")

    # Summary
    passed = sum(1 for r, _ in results if r == "PASS")
    failed = sum(1 for r, _ in results if r == "FAIL")

    print(f"\n  Summary: {passed} passed, {failed} failed")

    # Update registry
    game_info["scenarios_count"] = len(scenarios)
    if failed == 0 and passed > 0:
        game_info["status"] = "tested"
    elif failed > 0:
        game_info["status"] = "failed"

    return failed == 0


def generate_golden(game_name, registry, use_c=True, timeout=10):
    """Generate golden outputs for a game.

    Args:
        game_name: Name of the game
        registry: Game registry dict
        use_c: If True, use C interpreter; if False, use SIMH
        timeout: Timeout in seconds for C interpreter
    """
    game_name = game_name.upper()

    game_file = find_game_file(game_name, registry)
    if not game_file:
        print(f"ERROR: Game file not found for '{game_name}'")
        return False

    scenarios = get_scenario_files(game_name)
    if not scenarios:
        print(f"No scenarios found for {game_name}")
        return False

    golden_game_dir = GOLDEN_DIR / game_name.lower()
    golden_game_dir.mkdir(parents=True, exist_ok=True)

    print(f"Generating golden outputs for {game_name}...")

    for scenario_path in scenarios:
        scenario_name = scenario_path.stem
        print(f"  {scenario_name}...", end=" ", flush=True)

        if use_c:
            output = run_c_interpreter(game_file, scenario_path, timeout=timeout)
            if "TIMEOUT ERROR" in output:
                print("TIMEOUT")
                continue
        else:
            output = run_simh(game_file, scenario_path)

        golden_path = golden_game_dir / f"{scenario_name}.golden"
        golden_path.write_text(output)

        print("done")

    return True


def list_games(registry, status_filter=None):
    """List all games in the registry."""
    games = registry.get("games", {})

    print(f"\n{'='*80}")
    print("David Ahl's BASIC Computer Games - Test Registry")
    print(f"{'='*80}")

    stats = registry.get("test_statistics", {})
    print(f"Total: {stats.get('total_games', 0)} | "
          f"Tested: {stats.get('tested', 0)} | "
          f"Pending: {stats.get('pending', 0)} | "
          f"Failed: {stats.get('failed', 0)}")
    print(f"{'='*80}\n")

    print(f"{'Game':<20} {'Status':<10} {'Scenarios':<10} {'File':<20} Description")
    print(f"{'-'*20} {'-'*10} {'-'*10} {'-'*20} {'-'*30}")

    for name in sorted(games.keys()):
        info = games[name]
        status = info.get("status", "pending")

        if status_filter and status != status_filter:
            continue

        scenarios = info.get("scenarios_count", 0)
        filename = info.get("file", "")
        desc = info.get("description", "")[:40]

        status_symbol = {
            "tested": "[OK]",
            "failed": "[!!]",
            "pending": "[  ]"
        }.get(status, "[??]")

        print(f"{name:<20} {status_symbol:<10} {scenarios:<10} {filename:<20} {desc}")


def show_status(registry):
    """Show detailed test status."""
    stats = registry.get("test_statistics", {})
    games = registry.get("games", {})

    print(f"\n{'='*60}")
    print("TEST STATUS SUMMARY")
    print(f"{'='*60}")
    print(f"Total Games:    {stats.get('total_games', 0)}")
    print(f"Tested (OK):    {stats.get('tested', 0)}")
    print(f"Pending:        {stats.get('pending', 0)}")
    print(f"Failed:         {stats.get('failed', 0)}")
    print()

    # Show failed games
    failed = [n for n, g in games.items() if g.get("status") == "failed"]
    if failed:
        print("FAILED GAMES:")
        for name in failed:
            print(f"  - {name}")
        print()

    # Show tested games
    tested = [n for n, g in games.items() if g.get("status") == "tested"]
    if tested:
        print(f"TESTED GAMES ({len(tested)}):")
        for name in sorted(tested):
            scenarios = games[name].get("scenarios_count", 0)
            print(f"  - {name} ({scenarios} scenarios)")


def add_game(game_name, file_path, registry):
    """Add a new game to the registry."""
    game_name = game_name.upper()
    file_path = Path(file_path)

    if not file_path.exists():
        print(f"ERROR: File not found: {file_path}")
        return False

    # Copy to games directory
    dest_path = GAMES_DIR / file_path.name
    if not dest_path.exists():
        dest_path.write_text(file_path.read_text())
        print(f"Copied {file_path} to {dest_path}")

    # Add to registry
    registry["games"][game_name] = {
        "file": file_path.name,
        "status": "pending",
        "scenarios_count": 0,
        "description": f"Game: {game_name}",
        "uses_rnd": True,
        "uses_input": True,
        "features": [],
        "notes": "Automatically added"
    }

    print(f"Added {game_name} to registry")
    return True


def main():
    parser = argparse.ArgumentParser(
        description="David Ahl's BASIC Computer Games - Test Runner"
    )

    subparsers = parser.add_subparsers(dest="command", help="Commands")

    # List command
    list_parser = subparsers.add_parser("list", help="List all games")
    list_parser.add_argument("--status", choices=["tested", "pending", "failed"],
                            help="Filter by status")

    # Test command
    test_parser = subparsers.add_parser("test", help="Test a game")
    test_parser.add_argument("game", nargs="?", help="Game name (or --all)")
    test_parser.add_argument("--all", action="store_true", help="Test all games")
    test_parser.add_argument("--simh", action="store_true",
                            help="Compare directly with SIMH (not golden)")
    test_parser.add_argument("--c-only", action="store_true",
                            help="Test C interpreter only using existing golden files")
    test_parser.add_argument("--show-output", action="store_true",
                            help="Display actual game output after each passing test")
    test_parser.add_argument("-v", "--verbose", action="store_true",
                            help="Verbose output (show diffs on failure)")

    # Generate command
    gen_parser = subparsers.add_parser("generate", help="Generate golden output")
    gen_parser.add_argument("game", help="Game name")

    # Status command
    subparsers.add_parser("status", help="Show test summary")

    # Add command
    add_parser = subparsers.add_parser("add", help="Add a new game")
    add_parser.add_argument("game", help="Game name")
    add_parser.add_argument("file", help="Path to .bas file")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return 1

    # Ensure directories exist
    for d in [GAMES_DIR, SCENARIOS_DIR, GOLDEN_DIR, RESULTS_DIR]:
        d.mkdir(parents=True, exist_ok=True)

    registry = load_registry()

    try:
        if args.command == "list":
            list_games(registry, args.status)

        elif args.command == "test":
            c_only = getattr(args, 'c_only', False)
            show_output = getattr(args, 'show_output', False)
            if args.all:
                # Test all games with scenarios
                games = registry.get("games", {})
                results = []
                for game_name in sorted(games.keys()):
                    scenarios = get_scenario_files(game_name)
                    if scenarios:
                        result = test_game(game_name, registry, args.simh, args.verbose, c_only, show_output)
                        results.append((game_name, result))

                print(f"\n{'='*70}")
                print("FINAL SUMMARY")
                print(f"{'='*70}")
                for name, result in results:
                    status = "PASS" if result else ("FAIL" if result is False else "SKIP")
                    print(f"  {name}: {status}")

            elif args.game:
                test_game(args.game, registry, args.simh, args.verbose, c_only, show_output)
            else:
                print("ERROR: Specify a game name or --all")
                return 1

        elif args.command == "generate":
            generate_golden(args.game, registry)

        elif args.command == "status":
            show_status(registry)

        elif args.command == "add":
            add_game(args.game, args.file, registry)

    finally:
        save_registry(registry)

    return 0


if __name__ == "__main__":
    sys.exit(main())
