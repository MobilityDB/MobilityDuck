#!/usr/bin/env python3
"""
Results profiler for BerlinMOD benchmark
"""

import json
import sys
import os
import argparse
import pandas as pd

QUERIES_NUM = 17

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--benchmark', type=str, required=True)
    parser.add_argument('--type', type=str, default='native', choices=['native', 'accelerated'])
    benchmark = parser.parse_args().benchmark
    run_type = parser.parse_args().type

    if not os.path.exists(f'./results/output/{benchmark}'):
        print(f"Error: Output directory for benchmark '{benchmark}' does not exist.")
        sys.exit(1)
    if not os.path.exists(f'./results/output/{benchmark}/{run_type}'):
        print(f"Error: Output directory for run type '{run_type}' for benchmark '{benchmark}' does not exist.")
        sys.exit(1)

    results = dict()
    result_path = f'./results/output/{benchmark}/{run_type}'
    for i in range(1, QUERIES_NUM + 1):
        query_name = f'query_{i}'
        json_file = f'{result_path}/{query_name}.profile.json'
        if os.path.exists(json_file):
            with open(json_file, 'r') as f:
                profile_data = json.load(f)
                results[i] = {
                    'query': i,
                    'latency': profile_data.get('latency', -1),
                    'rows_returned': profile_data.get('rows_returned', -1)
                }
        if i == 10:
            json_file_create = f'{result_path}/{query_name}.create.profile.json'
            json_file_select = f'{result_path}/{query_name}.select.profile.json'
            if os.path.exists(json_file_create) and os.path.exists(json_file_select):
                with open(json_file_create, 'r') as f_create, open(json_file_select, 'r') as f_select:
                    profile_data_create = json.load(f_create)
                    profile_data_select = json.load(f_select)
                    results[i] = {
                        'query': i,
                        'latency': profile_data_create.get('latency', -1) + profile_data_select.get('latency', -1),
                        'rows_returned': profile_data_select.get('rows_returned', -1)
                    }

    df = pd.DataFrame(list(results.values()))
    stat_filepath = f'./results/stats/{benchmark}/run_{run_type}_profile.csv'
    df.to_csv(stat_filepath, index=False)
    print(f"Profile results saved to {stat_filepath}")

if __name__ == '__main__':
    main()