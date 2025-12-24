#!/usr/bin/env python3
"""
David Ahl's BASIC Computer Games - Unified Test Runner

Tests games from the classic 1978 book "BASIC Computer Games" against the
C implementation of Altair 8K BASIC to ensure 100% compatibility.

Games are automatically downloaded from:
https://github.com/coding-horror/basic-computer-games

Usage:
    ./ahl_test_runner.py test --all        # Test all games
    ./ahl_test_runner.py test GAME         # Test specific game
    ./ahl_test_runner.py list              # List all games and status
    ./ahl_test_runner.py status            # Show test summary
    ./ahl_test_runner.py clean             # Clean results and downloaded games
    ./ahl_test_runner.py download          # Download games only

Cross-platform compatible (Windows, macOS, Linux).
"""

import os
import sys
import json
import subprocess
import argparse
import difflib
import shutil
import re
import signal
from datetime import datetime
from pathlib import Path
from typing import Optional, Dict, List, Tuple, Any

# Directory structure
SCRIPT_DIR = Path(__file__).parent.absolute()
AHL_DIR = SCRIPT_DIR.parent
PROJECT_ROOT = AHL_DIR.parent.parent
GAMES_DIR = AHL_DIR / "games"
SCENARIOS_DIR = AHL_DIR / "scenarios"
GOLDEN_DIR = AHL_DIR / "golden"
RESULTS_DIR = AHL_DIR / "results"
REGISTRY_FILE = AHL_DIR / "game_registry.json"

# Import the downloader
sys.path.insert(0, str(SCRIPT_DIR))
try:
    from download_games import download_all_games, verify_games, clean_games, GAME_MAPPINGS
except ImportError:
    print("ERROR: Could not import download_games.py")
    print("Make sure download_games.py is in the same directory as this script.")
    sys.exit(1)


def get_interpreter_path() -> str:
    """Get the path to the C interpreter, building if necessary."""
    # Check environment variable first
    env_path = os.environ.get("BASIC8K_PATH")
    if env_path and Path(env_path).exists():
        return env_path

    # Check standard build locations
    build_dir = PROJECT_ROOT / "build"

    # Platform-specific executable name
    if sys.platform == "win32":
        exe_name = "basic8k.exe"
    else:
        exe_name = "basic8k"

    interpreter = build_dir / exe_name

    if interpreter.exists():
        return str(interpreter)

    # Try to build
    print("Interpreter not found. Attempting to build...")
    if build_interpreter():
        if interpreter.exists():
            return str(interpreter)

    print(f"ERROR: Could not find interpreter at {interpreter}")
    print("Please build the project first with: mkdir build && cd build && cmake .. && make")
    sys.exit(1)


def build_interpreter() -> bool:
    """Attempt to build the interpreter."""
    build_dir = PROJECT_ROOT / "build"

    try:
        # Create build directory
        build_dir.mkdir(exist_ok=True)

        # Run cmake
        result = subprocess.run(
            ["cmake", ".."],
            cwd=build_dir,
            capture_output=True,
            text=True
        )
        if result.returncode != 0:
            print(f"cmake failed: {result.stderr}")
            return False

        # Run make/build
        if sys.platform == "win32":
            build_cmd = ["cmake", "--build", "."]
        else:
            build_cmd = ["make", "-j4"]

        result = subprocess.run(
            build_cmd,
            cwd=build_dir,
            capture_output=True,
            text=True
        )
        if result.returncode != 0:
            print(f"Build failed: {result.stderr}")
            return False

        return True

    except FileNotFoundError as e:
        print(f"Build tools not found: {e}")
        return False
    except Exception as e:
        print(f"Build error: {e}")
        return False


def ensure_games_downloaded(quiet: bool = False) -> bool:
    """Ensure all games are downloaded."""
    present, missing_count, missing = verify_games(GAMES_DIR)

    if missing_count == 0:
        if not quiet:
            print(f"All {present} games present.")
        return True

    if not quiet:
        print(f"Missing {missing_count} games. Downloading...")

    results = download_all_games(GAMES_DIR, force=False, quiet=quiet)
    failed = sum(1 for v in results.values() if not v)

    return failed == 0


def load_registry() -> Dict[str, Any]:
    """Load the game registry."""
    if not REGISTRY_FILE.exists():
        return {"metadata": {}, "games": {}, "test_statistics": {}}
    with open(REGISTRY_FILE, 'r', encoding='utf-8') as f:
        return json.load(f)


def save_registry(registry: Dict[str, Any]) -> None:
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

    with open(REGISTRY_FILE, 'w', encoding='utf-8') as f:
        json.dump(registry, f, indent=2)


def find_game_file(game_name: str, registry: Dict[str, Any]) -> Optional[Path]:
    """Find the BASIC file for a game."""
    game_info = registry.get("games", {}).get(game_name.upper())
    if not game_info:
        return None

    filename = game_info.get("file")
    game_path = GAMES_DIR / filename

    if game_path.exists():
        return game_path

    return None


def get_scenario_files(game_name: str) -> List[Path]:
    """Get all scenario input files for a game."""
    scenarios = []
    game_dir = SCENARIOS_DIR / game_name.lower()
    if game_dir.exists():
        for f in sorted(game_dir.glob("*.input")):
            scenarios.append(f)
    return scenarios


def strip_banner(output: str) -> str:
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


def normalize_output(output: str) -> str:
    """Normalize output for comparison."""
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


def run_interpreter(program_path: Path, input_path: Optional[Path] = None,
                   timeout: int = 30) -> str:
    """Run a game on the C interpreter.

    Cross-platform compatible timeout handling.
    """
    interpreter = get_interpreter_path()

    input_data = ""
    if input_path and input_path.exists():
        with open(input_path, 'r', encoding='utf-8') as f:
            input_data = f.read()

    try:
        # Platform-specific process handling
        if sys.platform == "win32":
            # Windows: use subprocess with timeout
            proc = subprocess.Popen(
                [interpreter, str(program_path)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP
            )
            try:
                stdout, stderr = proc.communicate(input=input_data, timeout=timeout)
                output = stdout + stderr
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
                output = "TIMEOUT ERROR"
        else:
            # Unix: use process group for clean timeout
            proc = subprocess.Popen(
                [interpreter, str(program_path)],
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
                # Kill entire process group
                os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
                proc.wait()
                output = "TIMEOUT ERROR"

    except FileNotFoundError:
        output = f"ERROR: Interpreter not found at {interpreter}"
    except Exception as e:
        output = f"ERROR: {e}"

    return strip_banner(output)


def compare_outputs(c_output: str, golden_output: str,
                   game_name: str, scenario_name: str = "") -> Tuple[bool, Optional[str]]:
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


def test_game(game_name: str, registry: Dict[str, Any],
              verbose: bool = False, show_output: bool = False) -> Optional[bool]:
    """Test a single game against all its scenarios.

    Returns:
        True if all passed, False if any failed, None if no scenarios
    """
    game_name = game_name.upper()
    game_info = registry.get("games", {}).get(game_name)

    if not game_info:
        print(f"ERROR: Game '{game_name}' not found in registry")
        return False

    game_file = find_game_file(game_name, registry)
    if not game_file:
        print(f"ERROR: Game file not found for '{game_name}'")
        print(f"       Expected: {GAMES_DIR / game_info.get('file', 'unknown')}")
        return False

    print(f"\n{'='*70}")
    print(f"Testing: {game_name}")
    print(f"File: {game_file}")
    print(f"Description: {game_info.get('description', 'N/A')}")
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

        if not golden_path.exists():
            print(f"\n  Scenario: {scenario_name} [SKIP - no golden file]")
            results.append(("SKIP", scenario_name))
            continue

        print(f"\n  Scenario: {scenario_name}")

        # Run C interpreter
        print("    Running C interpreter...", end=" ", flush=True)
        c_output = run_interpreter(game_file, scenario_path)
        c_output_path = game_results_dir / f"{scenario_name}.c_output"
        c_output_path.write_text(c_output, encoding='utf-8')
        print("done")

        # Load golden output
        golden_output = golden_path.read_text(encoding='utf-8')

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
            diff_path.write_text(diff, encoding='utf-8')
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


def generate_golden(game_name: str, registry: Dict[str, Any],
                   timeout: int = 30) -> bool:
    """Generate golden outputs for a game using C interpreter."""
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

        output = run_interpreter(game_file, scenario_path, timeout=timeout)
        if "TIMEOUT ERROR" in output:
            print("TIMEOUT")
            continue

        golden_path = golden_game_dir / f"{scenario_name}.golden"
        golden_path.write_text(output, encoding='utf-8')

        print("done")

    return True


def list_games(registry: Dict[str, Any], status_filter: Optional[str] = None) -> None:
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


def show_status(registry: Dict[str, Any]) -> None:
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

    # Show pending games
    pending = [n for n, g in games.items() if g.get("status") == "pending"]
    if pending:
        print(f"PENDING GAMES ({len(pending)}):")
        for name in sorted(pending):
            print(f"  - {name}")


def clean_all(clean_games_too: bool = False, clean_results: bool = True) -> None:
    """Clean up generated files.

    Args:
        clean_games_too: If True, also remove downloaded games
        clean_results: If True, remove test results
    """
    print("Cleaning up...")

    if clean_results and RESULTS_DIR.exists():
        count = 0
        for item in RESULTS_DIR.iterdir():
            if item.is_dir():
                shutil.rmtree(item)
                count += 1
            elif item.is_file():
                item.unlink()
                count += 1
        print(f"  Removed {count} result items from {RESULTS_DIR}")

    if clean_games_too:
        removed = clean_games(GAMES_DIR)
        print(f"  Removed {removed} game files from {GAMES_DIR}")

    print("Done.")


def main() -> int:
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="David Ahl's BASIC Computer Games - Compatibility Test Runner",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s test --all          Test all games
  %(prog)s test HAMURABI       Test specific game
  %(prog)s list --status pending  List pending games
  %(prog)s clean               Clean results
  %(prog)s clean --games       Clean results AND downloaded games

Games are automatically downloaded from:
  https://github.com/coding-horror/basic-computer-games
"""
    )

    subparsers = parser.add_subparsers(dest="command", help="Commands")

    # Download command
    dl_parser = subparsers.add_parser("download", help="Download games only")
    dl_parser.add_argument("--force", "-f", action="store_true",
                          help="Re-download even if files exist")

    # List command
    list_parser = subparsers.add_parser("list", help="List all games")
    list_parser.add_argument("--status", choices=["tested", "pending", "failed"],
                            help="Filter by status")

    # Test command
    test_parser = subparsers.add_parser("test", help="Test games")
    test_parser.add_argument("game", nargs="?", help="Game name (or --all)")
    test_parser.add_argument("--all", action="store_true", help="Test all games")
    test_parser.add_argument("--show-output", action="store_true",
                            help="Display game output after each passing test")
    test_parser.add_argument("-v", "--verbose", action="store_true",
                            help="Verbose output (show diffs on failure)")
    test_parser.add_argument("--no-download", action="store_true",
                            help="Skip automatic game download")

    # Generate command
    gen_parser = subparsers.add_parser("generate", help="Generate golden output")
    gen_parser.add_argument("game", help="Game name")
    gen_parser.add_argument("--timeout", type=int, default=30,
                           help="Timeout in seconds (default: 30)")

    # Status command
    subparsers.add_parser("status", help="Show test summary")

    # Clean command
    clean_parser = subparsers.add_parser("clean", help="Clean up generated files")
    clean_parser.add_argument("--games", action="store_true",
                             help="Also remove downloaded games")
    clean_parser.add_argument("--results-only", action="store_true",
                             help="Only clean results (default)")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return 1

    # Ensure directories exist
    for d in [GAMES_DIR, SCENARIOS_DIR, GOLDEN_DIR, RESULTS_DIR]:
        d.mkdir(parents=True, exist_ok=True)

    # Handle clean command first (doesn't need games)
    if args.command == "clean":
        clean_all(
            clean_games_too=args.games,
            clean_results=not args.games or True
        )
        return 0

    # Handle download command
    if args.command == "download":
        results = download_all_games(GAMES_DIR, force=args.force, quiet=False)
        failed = sum(1 for v in results.values() if not v)
        return 0 if failed == 0 else 1

    # For other commands, ensure games are downloaded
    if args.command in ["test", "generate"]:
        if not getattr(args, 'no_download', False):
            if not ensure_games_downloaded(quiet=False):
                print("ERROR: Failed to download some games")
                return 1
            print()  # Blank line after download status

    registry = load_registry()

    try:
        if args.command == "list":
            list_games(registry, args.status)

        elif args.command == "test":
            if args.all:
                # Test all games with scenarios
                games = registry.get("games", {})
                results = []
                for game_name in sorted(games.keys()):
                    scenarios = get_scenario_files(game_name)
                    if scenarios:
                        result = test_game(game_name, registry, args.verbose,
                                         args.show_output)
                        results.append((game_name, result))

                print(f"\n{'='*70}")
                print("FINAL SUMMARY")
                print(f"{'='*70}")

                passed = failed = skipped = 0
                for name, result in results:
                    if result is True:
                        status = "PASS"
                        passed += 1
                    elif result is False:
                        status = "FAIL"
                        failed += 1
                    else:
                        status = "SKIP"
                        skipped += 1
                    print(f"  {name}: {status}")

                print(f"\nTotal: {passed} passed, {failed} failed, {skipped} skipped")

            elif args.game:
                test_game(args.game, registry, args.verbose, args.show_output)
            else:
                print("ERROR: Specify a game name or --all")
                return 1

        elif args.command == "generate":
            generate_golden(args.game, registry, args.timeout)

        elif args.command == "status":
            show_status(registry)

    finally:
        save_registry(registry)

    return 0


if __name__ == "__main__":
    sys.exit(main())
