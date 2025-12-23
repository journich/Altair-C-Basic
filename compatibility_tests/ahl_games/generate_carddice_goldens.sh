#!/bin/bash
# Generate golden files for card/dice game scenarios

BASIC="/Users/tb/dev/Altair-C-Basic/build/basic8k"
GAMES_DIR="/Users/tb/dev/Altair-C-Basic/compatibility_tests/ahl_games/games"
SCENARIOS_DIR="/Users/tb/dev/Altair-C-Basic/compatibility_tests/ahl_games/scenarios"

# List of card/dice games
GAMES=(aceyducey blackjack craps dice flipflop slots war)

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
            # Use timeout to prevent infinite loops (10 seconds should be plenty)
            timeout 10 "$BASIC" "$GAMES_DIR/${game}.bas" < "$input_file" > "$golden_file" 2>&1

            if [ $? -eq 124 ]; then
                echo "  WARNING: Timeout on $base - file may be incomplete"
                # Keep the partial file for now to review
            else
                echo "  Created: $golden_file"
            fi
        fi
    done
done

echo "Golden files generation complete!"
