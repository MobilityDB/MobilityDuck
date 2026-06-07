#!/usr/bin/env python3
"""Query runner for BerlinMOD benchmark."""

import subprocess
import time
import json
import sys
import os
import argparse
import re
from typing import Dict, Optional, Tuple

QUERIES_FILE = "./sql/berlinmod_r_queries.sql"
QUERIES_NUM = 17


def _parse_queries(queries_file: str) -> Dict[int, str]:
    """Split berlinmod_r_queries.sql into {query_num: sql} by -- QN: markers."""
    with open(queries_file, encoding="utf-8") as f:
        content = f.read()
    parts = re.split(r"(?=^-- Q\d+:)", content, flags=re.MULTILINE)
    queries: Dict[int, str] = {}
    for part in parts:
        m = re.match(r"-- Q(\d+):", part.strip())
        if m:
            n = int(m.group(1))
            sql = re.sub(r"^-- Q\d+:.*\n", "", part.strip(), count=1).strip()
            queries[n] = sql
    return queries


class QueryRunner:
    def __init__(
        self,
        duckdb_path: str = "../../build/release/duckdb",
        benchmark: str = "default",
        queries_file: str = QUERIES_FILE,
        output_path: str = "./results/output",
    ):
        self.duckdb_path = duckdb_path
        self.benchmark = benchmark
        self.queries_file = queries_file
        self.output_path = output_path
        self.queries_num = QUERIES_NUM
        self._queries: Optional[Dict[int, str]] = None

    def _get_queries(self) -> Dict[int, str]:
        if self._queries is None:
            self._queries = _parse_queries(self.queries_file)
        return self._queries

    def _run_sql(self, sql: str) -> subprocess.CompletedProcess:
        return subprocess.run(
            [self.duckdb_path, f"./databases/{self.benchmark}.db"],
            input=sql,
            capture_output=True,
            text=True,
        )

    def run_query(self, query_num: int) -> Tuple[float, int]:
        queries = self._get_queries()
        if query_num not in queries:
            print(f"\tError: Q{query_num} not found in {self.queries_file}")
            return -1, -1

        print(f"\nRunning Q{query_num}")
        output_file = f"{self.output_path}/{self.benchmark}/query_{query_num}.csv"
        full_sql = f".mode csv\n.output {output_file}\n{queries[query_num]}"

        start_time = time.time()
        while True:
            result = self._run_sql(full_sql)
            if result.returncode == 0:
                break
            print(f"\tError: {result.stderr}")
            print("\tRetrying...")
            time.sleep(1)
            start_time = time.time()

        elapsed = (time.time() - start_time) * 1000
        print(f"\tDone in {elapsed:.2f}ms")

        line_count = self._count_output_rows(output_file)
        print(f"\tOutput row count: {line_count}")
        return elapsed, line_count

    def _count_output_rows(self, output_file: str) -> int:
        if not os.path.exists(output_file):
            print(f"\tError: output file not found: {output_file}")
            return -1
        with open(output_file, encoding="utf-8") as f:
            count = sum(1 for _ in f)
        return max(0, count - 1)

    def run_queries(self) -> Dict:
        results: Dict = {}
        for n in range(1, self.queries_num + 1):
            elapsed, line_count = self.run_query(n)
            if elapsed != -1:
                results[f"query_{n}.sql"] = {
                    "elapsed": elapsed,
                    "row_count": line_count,
                }
        return results

    def run_explain(self) -> Dict:
        queries = self._get_queries()
        explain_dir = f"{self.output_path}/{self.benchmark}/explain"
        os.makedirs(explain_dir, exist_ok=True)

        results: Dict = {}
        for n in range(1, self.queries_num + 1):
            if n not in queries:
                continue
            output_file = f"{explain_dir}/query_{n}.txt"
            full_sql = (
                f".output {output_file}\n"
                f"SET memory_limit = '20GB';\n"
                f"EXPLAIN ANALYZE\n{queries[n]}"
            )
            print(f"\nRunning EXPLAIN Q{n}")
            result = self._run_sql(full_sql)
            if result.returncode != 0:
                print(f"\tError: {result.stderr}")
                continue

            elapsed = -1.0
            if os.path.exists(output_file):
                with open(output_file, encoding="utf-8") as f:
                    for line in f:
                        m = re.search(r"Total Time:\s*([0-9.]+)s", line)
                        if m:
                            try:
                                elapsed = float(m.group(1)) * 1000
                            except ValueError:
                                pass
                            break

            print(f"\tDone in {elapsed:.2f}ms")
            results[f"query_{n}.sql"] = elapsed
        return results


def main():
    parser = argparse.ArgumentParser(description="BerlinMOD query runner for DuckDB")
    parser.add_argument("--benchmark", type=str, required=True)
    parser.add_argument("--explain", type=int, default=0, choices=[0, 1])
    args = parser.parse_args()

    duckdb_path = "../../build/release/duckdb"
    if not os.path.exists(duckdb_path):
        print(f"Error: DuckDB not found at {duckdb_path}")
        sys.exit(1)

    output_path = "./results/output"
    os.makedirs(f"{output_path}/{args.benchmark}", exist_ok=True)

    runner = QueryRunner(duckdb_path, args.benchmark)
    results = runner.run_explain() if args.explain else runner.run_queries()

    stats_dir = f"./results/stats/{args.benchmark}"
    os.makedirs(stats_dir, exist_ok=True)
    stats_file = "run_explain.json" if args.explain else "run_queries.json"
    with open(f"{stats_dir}/{stats_file}", "w", encoding="utf-8") as f:
        json.dump(results, f, indent=4)
    print(f"\nResults saved to {stats_dir}/{stats_file}")


if __name__ == "__main__":
    main()
