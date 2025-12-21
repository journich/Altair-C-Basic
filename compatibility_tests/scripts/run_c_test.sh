#!/bin/bash
# Run a BASIC program on the C interpreter and capture output
# Usage: ./run_c_test.sh <program.bas> <output_file>

if [ $# -lt 2 ]; then
    echo "Usage: $0 <program.bas> <output_file>"
    exit 1
fi

PROGRAM="$1"
OUTFILE="$2"
BASIC_C="/Users/tb/dev/NEW-BASIC/mbasic2025/4k8k/8k/basic8k_c/build/basic8k"

# Run the program and capture output (skip the banner)
echo "RUN" | "$BASIC_C" "$PROGRAM" 2>&1 | \
    sed -n '/^===/,/^===/p' > "$OUTFILE"

# If the sed didn't find markers, just get everything after "BYTES FREE"
if [ ! -s "$OUTFILE" ]; then
    echo "RUN" | "$BASIC_C" "$PROGRAM" 2>&1 | \
        sed '1,/BYTES FREE/d' | \
        grep -v "^OK$" | \
        grep -v "^$" > "$OUTFILE"
fi
