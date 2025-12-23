#!/bin/bash
# Generate golden files for remaining game scenarios

BASIC="/Users/tb/dev/Altair-C-Basic/build/basic8k"
GAMES_DIR="/Users/tb/dev/Altair-C-Basic/compatibility_tests/ahl_games/games"
SCENARIOS_DIR="/Users/tb/dev/Altair-C-Basic/compatibility_tests/ahl_games/scenarios"

# List of games
GAMES=(amazing battle bug bullfight combat litquiz poker rockscissors roulette russianroulette salvo weekday)

for game in "${GAMES[@]}"; do
    echo "Processing $game..."

    # Find all scenario input files for this game
    for input_file in "$SCENARIOS_DIR/$game"/*.input; do
        if [ -f "$input_file" ]; then
            # Get the base filename without extension
            base=$(basename "$input_file" .input)

            # Create golden output file
            golden_file="${input_file%.input}.golden"

            echo "  Generating: $base"

            # Run the game with the input file and capture output
            "$BASIC" "$GAMES_DIR/${game}.bas" < "$input_file" > "$golden_file" 2>&1

            echo "  Created: $golden_file"
        fi
    done
done

echo "Golden files generation complete!"
