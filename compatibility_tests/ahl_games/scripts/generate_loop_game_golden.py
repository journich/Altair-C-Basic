#!/usr/bin/env python3
"""
Generate golden files for games with infinite loops.
Captures output until the game loops back to the beginning.
"""

import subprocess
import sys
import re
import signal
from pathlib import Path

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
            result.append(line.rstrip())

    # Remove trailing empty lines
    while result and not result[-1]:
        result.pop()

    return '\n'.join(result)

def run_with_timeout(program_path, input_path, timeout=3):
    """Run the C interpreter with a timeout."""
    input_data = ""
    if input_path and Path(input_path).exists():
        with open(input_path, 'r') as f:
            input_data = f.read()

    try:
        result = subprocess.run(
            ["/Users/tb/dev/Altair-C-Basic/build/basic8k", str(program_path)],
            input=input_data,
            capture_output=True,
            text=True,
            timeout=timeout
        )
        output = result.stdout + result.stderr
    except subprocess.TimeoutExpired as e:
        # Capture what we got before timeout
        output = (e.stdout.decode('utf-8', errors='replace') if e.stdout else "") + \
                 (e.stderr.decode('utf-8', errors='replace') if e.stderr else "")
    except Exception as e:
        output = f"ERROR: {e}"

    return strip_banner(output)

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: generate_loop_game_golden.py game.bas input.input output.golden [timeout]")
        sys.exit(1)

    program = sys.argv[1]
    input_file = sys.argv[2]
    output_file = sys.argv[3]
    timeout = int(sys.argv[4]) if len(sys.argv) > 4 else 3

    output = run_with_timeout(program, input_file, timeout)

    Path(output_file).write_text(output)
    print(f"Generated: {output_file}")
