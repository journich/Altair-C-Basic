#!/usr/bin/env python3
import subprocess
import re
from pathlib import Path

games_to_gen = ['stockmarket', 'pizza']
base_dir = Path('/Users/tb/dev/Altair-C-Basic/compatibility_tests/ahl_games')

for game in games_to_gen:
    scenarios_dir = base_dir / 'scenarios' / game
    golden_dir = base_dir / 'golden' / game
    golden_dir.mkdir(parents=True, exist_ok=True)

    for scenario_file in sorted(scenarios_dir.glob('*.input')):
        scenario_name = scenario_file.stem
        print(f"Generating {game}/{scenario_name}...")

        game_file = base_dir / 'games' / f"{game}.bas"
        with open(scenario_file, 'r') as f:
            input_data = f.read()

        result = subprocess.run(
            ['/Users/tb/dev/Altair-C-Basic/build/basic8k', str(game_file)],
            input=input_data,
            capture_output=True,
            text=True,
            timeout=30
        )

        output = result.stdout + result.stderr

        # Strip banner
        output = output.replace('\x07', '').replace('\r', '')
        ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
        output = ansi_escape.sub('', output)

        lines = output.split('\n')
        result_lines = []
        skip_banner = True

        for line in lines:
            if skip_banner:
                if any(x in line for x in ['MICROSOFT BASIC', 'ALTAIR', '[8K VERSION]', '[4K VERSION]', 'COPYRIGHT', 'BYTES FREE']) or line.strip() == 'OK':
                    continue
                if line.strip() == '':
                    continue
                if line.strip():
                    skip_banner = False

            if not skip_banner:
                result_lines.append(line.rstrip())

        # Remove leading/trailing empty lines
        while result_lines and not result_lines[0]:
            result_lines.pop(0)
        while result_lines and not result_lines[-1]:
            result_lines.pop()

        golden_file = golden_dir / f"{scenario_name}.golden"
        with open(golden_file, 'w') as f:
            f.write('\n'.join(result_lines))

        print(f"  Created {golden_file}")

print("Done!")
