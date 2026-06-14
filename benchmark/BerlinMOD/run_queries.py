#!/usr/bin/env python3
"""
Query runner for the BerlinMOD benchmark.

The query set is the SINGLE canonical BerlinMOD suite shared, unchanged, by all
three DB tools (MobilityDB / MobilityDuck / MobilitySpark): the `suite/` git
submodule (estebanzimanyi/berlinmod-portability), `q01.sql` .. `q17.sql` + the
round-trip `qrt.sql`.  The query files are pure portable SQL with NO per-tool
adaptation — the DuckDB-specific output handling (`.mode csv` / `.output`) is
applied here, by the harness, not embedded in the SQL.
"""

import subprocess
import time
import json
import sys
import os
import glob
import argparse
import re
from typing import Dict, List, Tuple

# The canonical suite (the single source shared by the 3 DB tools).
SUITE_PATH = "./suite"


class QueryRunner:
    def __init__(self, duckdb_path: str = "../../build/release/duckdb",
                       benchmark: str = "default",
                       suite_path: str = SUITE_PATH,
                       output_path: str = "./results/output"):
        self.duckdb_path = duckdb_path
        self.benchmark = benchmark
        self.suite_path = suite_path
        self.output_path = output_path

    def query_files(self) -> List[str]:
        """The canonical query files (q01..q17 + qrt), in order."""
        files = sorted(os.path.basename(p) for p in glob.glob(os.path.join(self.suite_path, "q*.sql")))
        # qrt sorts after q17 already; keep that order (q01..q17, qrt).
        return files

    def _canonical_sql(self, filename: str) -> str:
        with open(os.path.join(self.suite_path, filename), "r") as f:
            return f.read()

    def run_sql(self, filename: str) -> Tuple[float, int]:
        print(f"\nRunning {filename}")
        out_csv = f"results/output/{self.benchmark}/{filename.replace('.sql', '.csv')}"
        # Adaptation lives HERE, not in the canonical SQL: redirect DuckDB output.
        sql = f".mode csv\n.output {out_csv}\n" + self._canonical_sql(filename)

        success = False
        start_time = time.time()
        while not success:
            result = subprocess.run(
                [self.duckdb_path, f"./databases/{self.benchmark}.db"],
                input=sql, capture_output=True, text=True)
            if result.returncode == 0:
                success = True
            else:
                print(f"\tError running query: {result.stderr}")
                print("\tTrying again...")
                time.sleep(1)
                start_time = time.time()
        elapsed = (time.time() - start_time) * 1000  # ms
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
        return line_count - 1 if line_count > 0 else 0

    def run_queries(self) -> Dict:
        results = dict()
        for filename in self.query_files():
            elapsed, line_count = self.run_sql(filename)
            if elapsed != -1:
                results[filename] = {"elapsed": elapsed, "row_count": line_count}
        return results

    def run_explain_sql(self, filename: str) -> float:
        out_txt = f"results/output/{self.benchmark}/explain/{filename.replace('.sql', '.txt')}"
        # EXPLAIN ANALYZE is added here, around the canonical SQL — no separate
        # per-tool explain query files.
        sql = f".mode line\n.output {out_txt}\nEXPLAIN ANALYZE " + self._canonical_sql(filename)
        print(f"\nRunning EXPLAIN {filename}")
        success = False
        while not success:
            result = subprocess.run(
                [self.duckdb_path, f"./databases/{self.benchmark}.db"],
                input=sql, capture_output=True, text=True)
            if result.returncode == 0:
                success = True
            else:
                print(f"\tError running query: {result.stderr}")
                print("\tTrying again...")
                time.sleep(1)

        elapsed = -1.0
        with open(f"{self.output_path}/{self.benchmark}/explain/{filename.replace('.sql', '.txt')}", "r") as f:
            for line in f:
                m = re.search(r"Total Time:\s*([0-9.]+)s", line)
                if m:
                    try:
                        elapsed = float(m.group(1)) * 1000
                    except ValueError:
                        elapsed = -1.0
                    break
        print(f"\tDone in {elapsed:.2f}ms")
        return elapsed

    def run_explain(self) -> Dict:
        os.makedirs(f"./results/output/{self.benchmark}/explain", exist_ok=True)
        results = dict()
        for filename in self.query_files():
            elapsed = self.run_explain_sql(filename)
            if elapsed != -1:
                results[filename] = elapsed
        return results


def main():
    parser = argparse.ArgumentParser(description="Query runner for the BerlinMOD benchmark")
    parser.add_argument("--benchmark", type=str, required=True, help="Name of the benchmark run")
    parser.add_argument("--explain", type=int, default=0, choices=[0, 1], help="Run explain queries")
    args = parser.parse_args()
    benchmark, explain = args.benchmark, args.explain

    os.makedirs(f"./results/output/{benchmark}", exist_ok=True)

    duckdb_path = "../../build/release/duckdb"
    if not os.path.exists(duckdb_path):
        print(f"Error: DuckDB executable not found at {duckdb_path}")
        print("Please make sure you're running this from the benchmark directory and DuckDB is built.")
        sys.exit(1)
    if not os.path.exists(os.path.join(SUITE_PATH, "q01.sql")):
        print(f"Error: canonical suite not found at {SUITE_PATH}. Run: git submodule update --init benchmark/BerlinMOD/suite")
        sys.exit(1)

    runner = QueryRunner(duckdb_path, benchmark)
    results = runner.run_explain() if explain else runner.run_queries()

    os.makedirs(f"./results/stats/{benchmark}", exist_ok=True)
    stats_filename = "run_explain.json" if explain else "run_queries.json"
    with open(f"./results/stats/{benchmark}/{stats_filename}", "w") as f:
        json.dump(results, f, indent=4)
    print(f"\nResults saved to ./results/stats/{benchmark}/{stats_filename}")


if __name__ == "__main__":
    main()
