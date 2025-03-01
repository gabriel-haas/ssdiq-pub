#!/bin/bash

# Array of GC options
gc_options=("greedy" "2r-greedy" "2r-fifo")

# Array of skew factors
#zone_factors=("s0.5 f0.5 s0.5 f0.5" "s0.6 f0.4 s0.4 f0.6" "s0.7 f0.3 s0.3 f0.7" "s0.8 f0.2 s0.2 f0.8" "s0.9 f0.1 s0.1 f0.9" "s0.95 f0.05 s0.05 f0.95")

# Array of skew factors
zone_factors=("s0.95 f0.05 s0.05 f0.95")


cd ../build/

# Loop over each GC option
for gc in "${gc_options[@]}"; do
    # Loop over each skew factor
    for zones in "${zone_factors[@]}"; do
        # Run the command with current GC and skew factor
        SKEW="$skew" GC="$gc" PATTERN="zones" ZONES="$zones" ./sim/sim >> simzone.csv
    done
done
