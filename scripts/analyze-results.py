#!/usr/bin/env python3
"""
analyze-results.py - GC Benchmark Results Analyzer
Parses benchmark log files and generates summary statistics and comparisons
"""

import re
import sys
from pathlib import Path
from collections import defaultdict

def parse_gc_stats(content):
    """Parse gc-stats output from benchmark log"""
    # Extract the Result line with gc-stats
    result_match = re.search(r'Result: \((.*)\)', content, re.DOTALL)
    if not result_match:
        return None
    
    stats_str = result_match.group(1)
    stats = {}
    
    # Parse key-value pairs
    pairs = re.findall(r'\(([^)]+)\s+\.\s+([^)]+)\)', stats_str)
    for key, value in pairs:
        key = key.strip()
        value = value.strip()
        try:
            # Try to parse as number
            if '.' in value or 'e' in value.lower():
                stats[key] = float(value)
            else:
                stats[key] = int(value)
        except ValueError:
            stats[key] = value
    
    return stats

def analyze_results(results_dir):
    """Analyze all benchmark results"""
    results_path = Path(results_dir)
    if not results_path.exists():
        print(f"Error: Results directory '{results_dir}' not found")
        return
    
    # Collect all results
    benchmarks = defaultdict(dict)
    
    for log_file in results_path.glob('*.log'):
        # Parse filename: backend_benchmark.log
        parts = log_file.stem.split('_', 1)
        if len(parts) != 2:
            continue
        
        backend, benchmark = parts
        
        # Parse log content
        content = log_file.read_text()
        stats = parse_gc_stats(content)
        
        if stats:
            benchmarks[benchmark][backend] = stats
    
    # Generate report
    print("=" * 80)
    print("GC Performance Analysis Report")
    print("=" * 80)
    print()
    
    for benchmark in sorted(benchmarks.keys()):
        print(f"\n{'=' * 80}")
        print(f"Benchmark: {benchmark}")
        print('=' * 80)
        
        backends = benchmarks[benchmark]
        
        # Print comparison table
        if backends:
            print(f"\n{'Metric':<25} {'Mark-Sweep':<20} {'Copying':<20} {'Generational':<20}")
            print('-' * 85)
            
            # Key metrics to compare
            metrics = [
                ('collections', 'Collections'),
                ('total-gc-time-ms', 'Total GC Time (ms)'),
                ('max-pause-ms', 'Max Pause (ms)'),
                ('avg-pause-ms', 'Avg Pause (ms)'),
                ('objects-scanned', 'Objects Scanned'),
                ('objects-copied', 'Objects Copied'),
                ('survival-rate', 'Survival Rate'),
                ('metadata-bytes', 'Metadata (bytes)'),
            ]
            
            for key, label in metrics:
                values = []
                for backend in ['mark-sweep', 'copying', 'generational']:
                    if backend in backends and key in backends[backend]:
                        val = backends[backend][key]
                        if isinstance(val, float):
                            values.append(f"{val:.2f}")
                        else:
                            values.append(f"{val}")
                    else:
                        values.append("N/A")
                
                print(f"{label:<25} {values[0]:<20} {values[1]:<20} {values[2]:<20}")
            
            # Calculate speedup
            if 'mark-sweep' in backends and 'copying' in backends:
                ms_time = backends['mark-sweep'].get('total-gc-time-ms', 0)
                copy_time = backends['copying'].get('total-gc-time-ms', 0)
                if ms_time > 0 and copy_time > 0:
                    speedup = ms_time / copy_time
                    print(f"\n{'Copying Speedup:':<25} {speedup:.1f}x faster than Mark-Sweep")
    
    print("\n" + "=" * 80)
    print("Analysis Complete")
    print("=" * 80)

if __name__ == '__main__':
    if len(sys.argv) > 1:
        results_dir = sys.argv[1]
    else:
        results_dir = 'results'
    
    analyze_results(results_dir)
