#!/bin/bash
# run-gc-benchmarks.sh - Automated GC benchmark runner
# Runs all benchmarks across all GC backends and collects results

set -e

# Configuration
BACKENDS=("mark-sweep" "copying" "generational")
BENCHMARKS=("alloc-intensive" "mixed-lifetime" "pointer-dense" "fragmentation" "real-world")
RESULTS_DIR="results"
INTERPRETER="./interpreter"

# Create results directory
mkdir -p "$RESULTS_DIR"

# Print header
echo "========================================="
echo "GC Performance Benchmark Suite"
echo "========================================="
echo ""

# Run benchmarks
for backend in "${BACKENDS[@]}"; do
    echo "Testing backend: $backend"
    echo "-----------------------------------------"
    
    for benchmark in "${BENCHMARKS[@]}"; do
        echo "  Running: $benchmark"
        
        # Set output file
        output_file="$RESULTS_DIR/${backend}_${benchmark}.log"
        
        # Run benchmark with appropriate backend
        if [ "$backend" = "mark-sweep" ]; then
            $INTERPRETER -f "benchmarks/${benchmark}.lisp" > "$output_file" 2>&1
        else
            GC_BACKEND=$backend $INTERPRETER -f "benchmarks/${benchmark}.lisp" > "$output_file" 2>&1
        fi
        
        echo "    ✓ Completed (output: $output_file)"
    done
    
    echo ""
done

echo "========================================="
echo "All benchmarks completed!"
echo "Results saved to: $RESULTS_DIR/"
echo "========================================="
