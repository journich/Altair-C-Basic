#!/usr/bin/env python3
"""
Compatibility Test Harness for 8K BASIC

This script runs test programs on both the original SIMH-based 8K BASIC
and the C implementation, then compares the outputs.

Usage:
    ./test_harness.py generate   # Generate golden outputs from SIMH
    ./test_harness.py test       # Run tests and compare with golden
    ./test_harness.py all        # Generate golden and run tests
    ./test_harness.py c-only     # Run tests using C version only (no SIMH)
"""

import os
import sys
import subprocess
import re
import difflib
import tempfile
from pathlib import Path

# Paths
BASE_DIR = Path("/Users/tb/dev/NEW-BASIC/mbasic2025/4k8k/8k/basic8k_c/compatibility_tests")
PROGRAMS_DIR = BASE_DIR / "programs"
GOLDEN_DIR = BASE_DIR / "golden"
RESULTS_DIR = BASE_DIR / "results"
SCRIPTS_DIR = BASE_DIR / "scripts"
SIMH_DIR = Path("/Users/tb/dev/NEW-BASIC/mbasic2025/4k8k/8k")
C_BASIC = Path("/Users/tb/dev/NEW-BASIC/mbasic2025/4k8k/8k/basic8k_c/build/basic8k")


def get_test_programs():
    """Get list of test programs sorted by name."""
    programs = sorted(PROGRAMS_DIR.glob("*.bas"))
    return programs


def extract_output(text, require_run=False):
    """Extract the test output between === markers.

    If require_run is True, only extract after RUN appears in output.
    This is used for SIMH where we need to skip the program listing.
    """
    lines = text.split('\n')
    output = []
    capturing = False
    found_run = not require_run  # If not required, consider it found

    for line in lines:
        # Clean up the line - remove CR and extra whitespace
        line = line.replace('\r', '').rstrip()

        # Only start looking for markers after RUN command (if required)
        if require_run and 'RUN' in line and not found_run:
            found_run = True
            continue

        if not found_run:
            continue

        if '===' in line and 'END' not in line.upper():
            capturing = True
            output.append(line)
        elif '=== END' in line.upper():
            output.append(line)
            capturing = False
            break  # Stop after first complete test block
        elif capturing:
            output.append(line)

    return '\n'.join(output)


def run_on_simh(program_path):
    """Run a BASIC program on the original 8K BASIC via SIMH."""

    script_path = SCRIPTS_DIR / "run_simh_single.exp"

    try:
        result = subprocess.run(
            ['expect', str(script_path), str(program_path)],
            capture_output=True,
            text=True,
            timeout=90,
            cwd=str(SIMH_DIR)
        )
        output = result.stdout
    except subprocess.TimeoutExpired:
        return "ERROR: Timeout"
    except Exception as e:
        return f"ERROR: {str(e)}"

    # For SIMH, we need to skip past RUN to avoid program listing
    return extract_output(output, require_run=True)


def run_on_c(program_path):
    """Run a BASIC program on the C implementation.

    The C version auto-runs programs when loaded from file, so no RUN needed.
    We pass empty input and just capture the output.
    """

    try:
        result = subprocess.run(
            [str(C_BASIC), str(program_path)],
            input="",  # No RUN needed, program auto-runs
            capture_output=True,
            text=True,
            timeout=30
        )
        output = result.stdout + result.stderr
    except subprocess.TimeoutExpired:
        output = "ERROR: Timeout"
    except Exception as e:
        output = f"ERROR: {str(e)}"

    return extract_output(output)


def generate_golden(program_path):
    """Generate golden output for a test program."""
    test_name = program_path.stem
    print(f"  Generating golden output for {test_name}...", end=" ", flush=True)

    output = run_on_simh(program_path)

    if not output or output.startswith("ERROR"):
        print(f"FAILED: {output[:80] if output else 'empty'}")
        return None

    golden_path = GOLDEN_DIR / f"{test_name}.golden"
    with open(golden_path, 'w') as f:
        f.write(output)

    print("done")
    return output


def normalize_output(text):
    """Normalize output for comparison - handle whitespace and number formatting."""
    lines = []
    for line in text.strip().split('\n'):
        # Normalize multiple spaces to single space
        line = ' '.join(line.split())
        lines.append(line)
    return '\n'.join(lines)


def compare_outputs(golden, actual):
    """Compare golden and actual outputs, return diff if different."""
    # Normalize both outputs
    golden_norm = normalize_output(golden)
    actual_norm = normalize_output(actual)

    golden_lines = golden_norm.split('\n')
    actual_lines = actual_norm.split('\n')

    if golden_lines == actual_lines:
        return None

    diff = list(difflib.unified_diff(
        golden_lines, actual_lines,
        fromfile='golden', tofile='actual',
        lineterm=''
    ))

    return '\n'.join(diff)


def run_test(program_path):
    """Run a test and compare with golden output."""
    test_name = program_path.stem
    golden_path = GOLDEN_DIR / f"{test_name}.golden"

    if not golden_path.exists():
        return test_name, "SKIP", "No golden output file"

    # Read golden output
    with open(golden_path, 'r') as f:
        golden = f.read().strip()

    # Run on C implementation
    actual = run_on_c(program_path)

    # Compare
    diff = compare_outputs(golden, actual)

    if diff is None:
        return test_name, "PASS", None
    else:
        # Save the actual output and diff
        result_path = RESULTS_DIR / f"{test_name}.actual"
        with open(result_path, 'w') as f:
            f.write(actual)

        diff_path = RESULTS_DIR / f"{test_name}.diff"
        with open(diff_path, 'w') as f:
            f.write(diff)

        return test_name, "FAIL", diff


def cmd_generate():
    """Generate all golden outputs."""
    print("Generating golden outputs from SIMH...")
    print("=" * 60)

    programs = get_test_programs()
    success_count = 0
    for program in programs:
        # Skip input-requiring tests
        if 'input' in program.stem.lower():
            print(f"  Skipping {program.stem} (requires input)")
            continue
        result = generate_golden(program)
        if result:
            success_count += 1

    print("=" * 60)
    print(f"Generated {success_count} golden outputs")


def cmd_test():
    """Run all tests and compare with golden outputs."""
    print("Running compatibility tests...")
    print("=" * 60)

    programs = get_test_programs()
    results = {"PASS": 0, "FAIL": 0, "SKIP": 0}
    failures = []

    for program in programs:
        test_name, status, detail = run_test(program)
        results[status] += 1

        status_symbol = {"PASS": "✓", "FAIL": "✗", "SKIP": "-"}[status]
        print(f"  [{status_symbol}] {test_name}: {status}")

        if status == "FAIL":
            failures.append((test_name, detail))

    print("=" * 60)
    print(f"Results: {results['PASS']} passed, {results['FAIL']} failed, {results['SKIP']} skipped")

    if failures:
        print("\nFailures:")
        for test_name, diff in failures:
            print(f"\n--- {test_name} ---")
            print(diff[:1000] if diff else "No diff available")
            if diff and len(diff) > 1000:
                print("... (truncated)")

    return results['FAIL'] == 0


def cmd_c_only():
    """Run C version tests without comparison (just verify they run)."""
    print("Running C version tests...")
    print("=" * 60)

    programs = get_test_programs()
    results = {"PASS": 0, "FAIL": 0}

    for program in programs:
        test_name = program.stem
        # Skip input-requiring tests
        if 'input' in test_name.lower():
            print(f"  [-] {test_name}: SKIP (requires input)")
            continue

        output = run_on_c(program)
        if output and not output.startswith("ERROR") and "===" in output:
            results["PASS"] += 1
            print(f"  [✓] {test_name}: PASS")

            # Save output
            result_path = RESULTS_DIR / f"{test_name}.c_output"
            with open(result_path, 'w') as f:
                f.write(output)
        else:
            results["FAIL"] += 1
            print(f"  [✗] {test_name}: FAIL")
            print(f"      Output: {output[:100]}...")

    print("=" * 60)
    print(f"Results: {results['PASS']} passed, {results['FAIL']} failed")

    return results['FAIL'] == 0


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    command = sys.argv[1]

    # Ensure directories exist
    GOLDEN_DIR.mkdir(exist_ok=True)
    RESULTS_DIR.mkdir(exist_ok=True)

    if command == "generate":
        cmd_generate()
    elif command == "test":
        success = cmd_test()
        sys.exit(0 if success else 1)
    elif command == "all":
        cmd_generate()
        print()
        success = cmd_test()
        sys.exit(0 if success else 1)
    elif command == "c-only":
        success = cmd_c_only()
        sys.exit(0 if success else 1)
    else:
        print(f"Unknown command: {command}")
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
