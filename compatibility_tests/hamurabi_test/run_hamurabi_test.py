#!/usr/bin/env python3
"""
HAMURABI Compatibility Test Runner

Tests the HAMURABI game with 10 different input scenarios on both:
- C implementation (basic8k)
- Original 8080 BASIC via SIMH

Compares outputs to verify identical behavior.
"""

import os
import sys
import subprocess
import tempfile
import difflib

# Paths
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(os.path.dirname(SCRIPT_DIR))
HAMURABI_BAS = os.path.join(PROJECT_ROOT, "compatibility_tests/programs/hamurabi.bas")
C_INTERPRETER = "/Users/tb/dev/NEW-BASIC/mbasic2025/4k8k/8k/basic8k_c/build/basic8k"
SIMH_DIR = "/Users/tb/dev/NEW-BASIC/mbasic2025/4k8k/8k"
RESULTS_DIR = os.path.join(SCRIPT_DIR, "results")

SCENARIOS = [
    ("scenario_01_instant_death", "Instant death - feed 0 people"),
    ("scenario_02_early_death", "Early death - underfeed year 2-3"),
    ("scenario_03_slow_decline", "Slow decline - chronic underfeeding"),
    ("scenario_04_conservative", "Conservative - safe steady play"),
    ("scenario_05_land_baron", "Land baron - aggressive expansion"),
    ("scenario_06_max_planting", "Max planting - plant everything"),
    ("scenario_07_balanced", "Balanced - moderate growth"),
    ("scenario_08_sell_land", "Sell land - liquidate for survival"),
    ("scenario_09_population", "Population focus - feed generously"),
    ("scenario_10_optimal", "Optimal - aim for best ending"),
]


def run_c_interpreter(program_path, input_path, output_path):
    """Run HAMURABI on the C interpreter with scripted input."""
    with open(input_path, 'r') as f:
        input_data = f.read()

    # C interpreter auto-runs when given a program file, no RUN command needed
    full_input = input_data

    try:
        result = subprocess.run(
            [C_INTERPRETER, program_path],
            input=full_input,
            capture_output=True,
            text=True,
            timeout=30
        )
        output = result.stdout + result.stderr
    except subprocess.TimeoutExpired:
        output = "TIMEOUT ERROR"
    except Exception as e:
        output = f"ERROR: {e}"

    # Strip banner and normalize
    output = strip_banner(output)

    with open(output_path, 'w') as f:
        f.write(output)

    return output


def run_simh(program_path, input_path, output_path):
    """Run HAMURABI on SIMH with scripted input."""

    expect_script = os.path.join(SCRIPT_DIR, "run_simh_hamurabi.exp")

    try:
        result = subprocess.run(
            [expect_script, program_path, input_path],
            capture_output=True,
            text=True,
            timeout=180
        )
        output = result.stdout
        if result.stderr:
            print(f"    SIMH stderr: {result.stderr[:200]}")
    except subprocess.TimeoutExpired:
        output = "TIMEOUT ERROR"
    except Exception as e:
        output = f"ERROR: {e}"

    # Strip control characters and normalize
    output = strip_simh_output(output)

    with open(output_path, 'w') as f:
        f.write(output)

    return output


def strip_banner(output):
    """Strip the C interpreter banner from output."""
    # Remove bell characters (CHR$(7)) - both interpreters output them,
    # but SIMH terminal doesn't pass them through to capture
    output = output.replace('\x07', '')

    lines = output.split('\n')
    result = []
    started = False
    for line in lines:
        if 'HAMURABI' in line and not started:
            started = True
        if started:
            result.append(line)
    return '\n'.join(result)


def strip_simh_output(output):
    """Clean up SIMH output - remove control characters and normalize."""
    import re

    # Remove ANSI escape codes
    ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
    output = ansi_escape.sub('', output)

    # Remove carriage returns
    output = output.replace('\r', '')

    # Remove bell characters
    output = output.replace('\x07', '')

    # Find the start of actual output (after RUN)
    lines = output.split('\n')
    result = []
    started = False
    for line in lines:
        if 'HAMURABI' in line and not started:
            started = True
        if started:
            result.append(line)

    return '\n'.join(result)


def normalize_output(output):
    """Normalize output for comparison."""
    lines = output.strip().split('\n')
    normalized = []
    for line in lines:
        # Strip trailing whitespace only
        line = line.rstrip()
        # Skip empty lines at start/end
        if line or normalized:
            normalized.append(line)

    # Remove trailing empty lines
    while normalized and not normalized[-1]:
        normalized.pop()

    return '\n'.join(normalized)


def compare_outputs(c_output, simh_output, scenario_name):
    """Compare outputs and return (match, diff)."""
    c_norm = normalize_output(c_output)
    simh_norm = normalize_output(simh_output)

    if c_norm == simh_norm:
        return True, None

    # Generate diff
    c_lines = c_norm.split('\n')
    simh_lines = simh_norm.split('\n')

    diff = list(difflib.unified_diff(
        simh_lines, c_lines,
        fromfile=f'{scenario_name}.simh',
        tofile=f'{scenario_name}.c',
        lineterm=''
    ))

    return False, '\n'.join(diff)


def main():
    os.makedirs(RESULTS_DIR, exist_ok=True)

    print("=" * 70)
    print("HAMURABI Compatibility Test")
    print("=" * 70)
    print(f"C Interpreter: {C_INTERPRETER}")
    print(f"SIMH Directory: {SIMH_DIR}")
    print(f"Program: {HAMURABI_BAS}")
    print("=" * 70)
    print()

    # Check prerequisites
    if not os.path.exists(C_INTERPRETER):
        print(f"ERROR: C interpreter not found at {C_INTERPRETER}")
        sys.exit(1)

    if not os.path.exists(HAMURABI_BAS):
        print(f"ERROR: HAMURABI.BAS not found at {HAMURABI_BAS}")
        sys.exit(1)

    results = []

    for scenario_name, description in SCENARIOS:
        input_path = os.path.join(SCRIPT_DIR, f"{scenario_name}.input")
        c_output_path = os.path.join(RESULTS_DIR, f"{scenario_name}.c_output")
        simh_output_path = os.path.join(RESULTS_DIR, f"{scenario_name}.simh_output")
        diff_path = os.path.join(RESULTS_DIR, f"{scenario_name}.diff")

        if not os.path.exists(input_path):
            print(f"  [SKIP] {scenario_name}: input file not found")
            results.append((scenario_name, "SKIP", description))
            continue

        print(f"Testing: {scenario_name}")
        print(f"  Description: {description}")

        # Run C interpreter
        print("  Running C interpreter...", end=" ", flush=True)
        c_output = run_c_interpreter(HAMURABI_BAS, input_path, c_output_path)
        print("done")

        # Run SIMH
        print("  Running SIMH...", end=" ", flush=True)
        simh_output = run_simh(HAMURABI_BAS, input_path, simh_output_path)
        print("done")

        # Compare
        match, diff = compare_outputs(c_output, simh_output, scenario_name)

        if match:
            print(f"  Result: PASS ✓")
            results.append((scenario_name, "PASS", description))
        else:
            print(f"  Result: FAIL ✗")
            with open(diff_path, 'w') as f:
                f.write(diff)
            print(f"  Diff saved to: {diff_path}")
            results.append((scenario_name, "FAIL", description))

        print()

    # Summary
    print("=" * 70)
    print("SUMMARY")
    print("=" * 70)

    passed = sum(1 for _, status, _ in results if status == "PASS")
    failed = sum(1 for _, status, _ in results if status == "FAIL")
    skipped = sum(1 for _, status, _ in results if status == "SKIP")

    for name, status, desc in results:
        symbol = "✓" if status == "PASS" else ("✗" if status == "FAIL" else "-")
        print(f"  [{symbol}] {name}: {status} - {desc}")

    print()
    print(f"Results: {passed} passed, {failed} failed, {skipped} skipped")
    print("=" * 70)

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
