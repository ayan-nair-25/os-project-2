#!/bin/bash

# Check if a policy is provided as an argument
if [ -z "$1" ]; then
    echo "Usage: $0 {SCHED_POLICY}"
    exit 1
fi

# Store the input policy
SCHED_POLICY=$1

# Run the commands with the provided scheduling policy
make clean
make SCHED=$SCHED_POLICY

cd benchmarks/ || { echo "Failed to enter benchmarks directory"; exit 1; }

make clean
make SCHED=$SCHED_POLICY
./genRecord.sh
