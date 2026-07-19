#!/usr/bin/env python3
"""
Query runner for BerlinMOD benchmark
"""

import subprocess
import time
import json
import sys
import os
import argparse
from typing import Dict, Tuple

QUERIES_NUM = 17

class QueryRunner:
    def __init__(self, duckdb_path: str = "../../build/release/duckdb",
                       benchmark: str = "default",
                       queries_path: str = "./sql",
                       output_path: str = "./results/output"):
        self.duckdb_path = duckdb_path
        self.benchmark = benchmark
        self.queries_path = queries_path
        self.output_path = output_path
        self.queries_num = QUERIES_NUM

    def run_sql(self, filename: str, run_type: str) -> Tuple[float, int]:
        success = False
        start_time = time.time()
        sql_path = os.path.join(self.queries_path, run_type, filename)
        print(f"\nRunning {sql_path}")
        if not os.path.exists(sql_path):
            print(f"\tError: Query file not found: {sql_path}")
            return -1, -1
        
        with open(sql_path, "r") as f:
            sql = f.read()

        # sql = sql.replace(f".output results/output/{run_type}/query", f".output results/output/{self.benchmark}/{run_type}/query")
        prefix_text = f"""
.mode csv
.output results/output/{self.benchmark}/{run_type}/{filename.replace('.sql', '.csv')}
SET memory_limit = '20GB';
PRAGMA enable_profiling = 'json';
PRAGMA profiling_output = 'results/output/{self.benchmark}/{run_type}/{filename.replace('.sql', '.profile.json')}';
"""
        sql = prefix_text + sql

        while not success:
            result = subprocess.run(
                [self.duckdb_path, f"./databases/{self.benchmark}.db"],
                input=sql,
                capture_output=True,
                text=True
            )
            if result.returncode == 0:
                success = True
            else:
                print(f"\tError running query: {result.stderr}")
                print("\tTrying again...")
                time.sleep(1)
                start_time = time.time()

        end_time = time.time()
        elapsed = (end_time - start_time) * 1000 # milliseconds

        print(f"\tDone in {elapsed:.2f}ms")

        line_count = self.run_validation(filename, run_type)
        print(f"\tOutput row count: {line_count}")
        
        return elapsed, line_count

    def run_validation(self, filename: str, run_type: str) -> int:
        output_file = f"{self.output_path}/{self.benchmark}/{run_type}/{filename.replace('.sql', '.csv')}"
        if not os.path.exists(output_file):
            print(f"\tError: Output file not found: {output_file}")
            return -1
        with open(output_file, "r") as f:
            line_count = sum(1 for _ in f)
        if line_count > 0:
            line_count -= 1
        return line_count

    def run_queries(self, run_type: str) -> Dict:
        print(f"Running queries for benchmark '{self.benchmark}' with run type '{run_type}'")
        results = dict()

        for query_num in range(1, self.queries_num + 1):
            filename = f"query_{query_num}.sql"
            elapsed, line_count = self.run_sql(filename, run_type)
            if elapsed != -1:
                results[filename] = {
                    "elapsed": elapsed,
                    "row_count": line_count
                }
        
        return results

def main():
    parser = argparse.ArgumentParser(description="Data loader for BerlinMOD benchmark")
    parser.add_argument("--benchmark", type=str, required=True, help="Name of the benchmark run")
    parser.add_argument("--type", type=str, default="native", choices=["native", "accelerated"])
    benchmark = parser.parse_args().benchmark
    run_type = parser.parse_args().type

    if not os.path.exists(f"./results/output/{benchmark}"):
        os.makedirs(f"./results/output/{benchmark}")
    if not os.path.exists(f"./results/output/{benchmark}/{run_type}"):
        os.makedirs(f"./results/output/{benchmark}/{run_type}")

    duckdb_path = "../../build/release/duckdb"
    if not os.path.exists(duckdb_path):
        print(f"Error: DuckDB executable not found at {duckdb_path}")
        print("Please make sure you're running this from the benchmark directory and DuckDB is built.")
        sys.exit(1)
    
    runner = QueryRunner(duckdb_path, benchmark)
    results = runner.run_queries(run_type)
    
    if not os.path.exists(f"./results/stats/{benchmark}"):
        os.makedirs(f"./results/stats/{benchmark}")
    
    stats_filename = f"run_{run_type}.json"
    with open(f"./results/stats/{benchmark}/{stats_filename}", "w") as f:
        json.dump(results, f, indent=4)
    
    print(f"\nResults saved to ./results/stats/{benchmark}/{stats_filename}")

if __name__ == "__main__":
    main()