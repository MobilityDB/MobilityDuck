#!/usr/bin/env python3
"""
Compare results of MobilityDuck between native and accelerated queries
"""

import json
import sys
import os
import argparse
import pandas as pd

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--benchmark', type=str, required=True)
    benchmark = parser.parse_args().benchmark

    native_file = f'./results/stats/{benchmark}/run_native_profile.csv'
    accelerated_file = f'./results/stats/{benchmark}/run_accelerated_profile.csv'

    if not os.path.exists(native_file):
        print(f"Error: Native profile results file '{native_file}' does not exist.")
        sys.exit(1)
    if not os.path.exists(accelerated_file):
        print(f"Error: Accelerated profile results file '{accelerated_file}' does not exist.")
        sys.exit(1)

    native_df = pd.read_csv(native_file)
    accelerated_df = pd.read_csv(accelerated_file)

    comparison_df = pd.merge(native_df, accelerated_df, on='query', suffixes=('_native', '_accelerated'))
    comparison_df['latency_diff'] = comparison_df['latency_native'] - comparison_df['latency_accelerated']
    comparison_df['rows_returned_diff'] = comparison_df['rows_returned_native'] - comparison_df['rows_returned_accelerated']

    comparison_filepath = f'./results/stats/{benchmark}/comparison_profile.csv'
    comparison_df.to_csv(comparison_filepath, index=False)
    print(f"Comparison results saved to {comparison_filepath}")

if __name__ == '__main__':
    main()