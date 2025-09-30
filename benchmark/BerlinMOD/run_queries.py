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
import re
from typing import Dict, Tuple

QUERIES_NUM = 17

class QueryRunner:
    def __init__(self, duckdb_path: str = "../../build/release/duckdb",
                       benchmark: str = "default",
                       queries_path: str = "./sql/queries",
                       explain_path: str = "./sql/explain",
                       output_path: str = "./results/output"):
        self.duckdb_path = duckdb_path
        self.benchmark = benchmark
        self.queries_path = queries_path
        self.explain_path = explain_path
        self.output_path = output_path
        self.queries_num = QUERIES_NUM

    def run_sql(self, filename: str) -> Tuple[float, int]:
        success = False
        print(f"\nRunning {filename}")
        start_time = time.time()
        sql_path = os.path.join(self.queries_path, filename)
        if not os.path.exists(sql_path):
            print(f"\tError: Query file not found: {sql_path}")
            return -1, -1
        
        with open(sql_path, "r") as f:
            sql = f.read()

        sql = sql.replace(".output results/output/query", f".output results/output/{self.benchmark}/query")

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

        line_count = self.run_validation(filename)
        print(f"\tOutput row count: {line_count}")
        
        return elapsed, line_count

    def run_validation(self, filename: str) -> int:
        output_file = f"{self.output_path}/{self.benchmark}/{filename.replace('.sql', '.csv')}"
        if not os.path.exists(output_file):
            print(f"\tError: Output file not found: {output_file}")
            return -1
        with open(output_file, "r") as f:
            line_count = sum(1 for _ in f)
        if line_count > 0:
            line_count -= 1
        return line_count

    def run_queries(self) -> Dict:
        results = dict()

        for query_num in range(1, self.queries_num + 1):
            filename = f"query_{query_num}.sql"
            elapsed, line_count = self.run_sql(filename)
            if elapsed != -1:
                results[filename] = {
                    "elapsed": elapsed,
                    "row_count": line_count
                }
        
        return results

    def run_explain_sql(self, filename: str) -> Tuple[float, int]:
        output_file = f"{self.output_path}/{self.benchmark}/explain/{filename.replace('.sql', '.txt')}"

        success = False
        print(f"\nRunning {filename}")
        sql_path = os.path.join(self.explain_path, filename)
        if not os.path.exists(sql_path):
            print(f"\tError: Query file not found: {sql_path}")
            return -1, -1
        
        with open(sql_path, "r") as f:
            sql = f.read()
        
        sql = sql.replace(".output results/output/explain", f".output results/output/{self.benchmark}/explain")

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

        with open(output_file, "r") as f:
            output = f.readlines()

        elapsed = -1.0
        for line in output:
            match = re.search(r"Total Time:\s*([0-9.]+)s", line)
            if match:
                try:
                    elapsed = float(match.group(1)) * 1000  # convert to ms
                except ValueError:
                    elapsed = -1.0
                break

        print(f"\tDone in {elapsed:.2f}ms")

        return elapsed

    def run_explain(self) -> Dict:
        if not os.path.exists(f"./results/output/{self.benchmark}/explain"):
            os.makedirs(f"./results/output/{self.benchmark}/explain")

        results = dict()
        for query_num in range(1, self.queries_num + 1):
            filename = f"query_{query_num}.sql"
            elapsed = self.run_explain_sql(filename)
            if elapsed != -1:
                results[filename] = elapsed
        return results

def main():
    parser = argparse.ArgumentParser(description="Data loader for BerlinMOD benchmark")
    parser.add_argument("--benchmark", type=str, required=True, help="Name of the benchmark run")
    parser.add_argument("--explain", type=int, default=0, choices=[0, 1], help="Run explain queries")
    benchmark = parser.parse_args().benchmark
    explain = parser.parse_args().explain

    if not os.path.exists(f"./results/output/{benchmark}"):
        os.makedirs(f"./results/output/{benchmark}")

    duckdb_path = "../../build/release/duckdb"
    if not os.path.exists(duckdb_path):
        print(f"Error: DuckDB executable not found at {duckdb_path}")
        print("Please make sure you're running this from the benchmark directory and DuckDB is built.")
        sys.exit(1)
    
    runner = QueryRunner(duckdb_path, benchmark)
    if explain:
        results = runner.run_explain()
    else:
        results = runner.run_queries()
    
    if not os.path.exists(f"./results/stats/{benchmark}"):
        os.makedirs(f"./results/stats/{benchmark}")
    
    stats_filename = "run_queries.json" if not explain else "run_explain.json"
    with open(f"./results/stats/{benchmark}/{stats_filename}", "w") as f:
        json.dump(results, f, indent=4)
    
    print(f"\nResults saved to ./results/stats/{benchmark}/{stats_filename}")

if __name__ == "__main__":
    main()