#!/usr/bin/env python3
#
# This MobilityDB code is provided under The PostgreSQL License.
# Copyright (c) 2016-2026, Université libre de Bruxelles and MobilityDB
# contributors
#
# MobilityDB includes portions of PostGIS version 3 source code released
# under the GNU General Public License (GPLv2 or later).
# Copyright (c) 2001-2026, PostGIS contributors
#
# Permission to use, copy, modify, and distribute this software and its
# documentation for any purpose, without fee, and without a written
# agreement is hereby granted, provided that the above copyright notice and
# this paragraph and the following two paragraphs appear in all copies.
#
# IN NO EVENT SHALL UNIVERSITE LIBRE DE BRUXELLES BE LIABLE TO ANY PARTY FOR
# DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
# LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
# EVEN IF UNIVERSITE LIBRE DE BRUXELLES HAS BEEN ADVISED OF THE POSSIBILITY
# OF SUCH DAMAGE.
#
# UNIVERSITE LIBRE DE BRUXELLES SPECIFICALLY DISCLAIMS ANY WARRANTIES,
# INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
# AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON
# AN "AS IS" BASIS, AND UNIVERSITE LIBRE DE BRUXELLES HAS NO OBLIGATIONS TO
# PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
#

"""
Data loader for BerlinMOD benchmark
"""

import subprocess
import time
import json
import sys
import os
import argparse
from typing import Dict

QUERIES = [
    "00_cleanup.sql",
    "01_instants.sql",
    "02_periods.sql",
    "03_points.sql",
    "04_regions.sql",
    "05_vehicles.sql",
    "06_licences.sql",
    "07_trips.sql",
    "08_cleanup.sql"
]

VALIDATION_QUERIES = {
    "01_instants.sql": ".mode line\nSELECT COUNT(*) FROM Instants",
    "02_periods.sql": ".mode line\nSELECT COUNT(*) FROM Periods",
    "03_points.sql": ".mode line\nSELECT COUNT(*) FROM Points",
    "04_regions.sql": ".mode line\nSELECT COUNT(*) FROM Regions",
    "05_vehicles.sql": ".mode line\nSELECT COUNT(*) FROM Vehicles",
    "06_licences.sql": ".mode line\nSELECT COUNT(*) FROM Licences",
    "07_trips.sql": ".mode line\nSELECT COUNT(*) FROM Trips"
}

class DataLoader:
    def __init__(self, duckdb_path: str = "../../build/release/duckdb",
                       benchmark: str = "default",
                       queries_path: str = "./sql/load"):
        self.duckdb_path = duckdb_path
        self.benchmark = benchmark
        self.queries_path = queries_path
        self.queries = QUERIES
        self.validation_queries = VALIDATION_QUERIES

    def run_sql(self, filename: str) -> float:
        print(f"\nRunning {filename}")
        start_time = time.time()
        sql_path = os.path.join(self.queries_path, filename)
        with open(sql_path, "r") as f:
            sql = f.read()

        sql = sql.replace("./data/", f"./data/{self.benchmark}/")

        result = subprocess.run(
            [self.duckdb_path, f"./databases/{self.benchmark}.db"],
            input=sql,
            capture_output=True,
            text=True
        )

        end_time = time.time()
        elapsed = (end_time - start_time) * 1000 # milliseconds

        if result.returncode != 0:
            print(f"\tError running query: {result.stderr}")
            return -1
        
        print(f"\tDone in {elapsed:.2f}ms")

        if filename in self.validation_queries:
            validation_query = self.validation_queries[filename]
            validation_result = subprocess.run(
                [self.duckdb_path, f"./databases/{self.benchmark}.db"],
                input=validation_query,
                capture_output=True,
                text=True,
                timeout=300
            )
            if validation_result.returncode != 0:
                print(f"\nError running validation query: {validation_result.stderr}")
                return -1
            
            print(f"\t{filename} count: {validation_result.stdout.strip()}")
            val_result = validation_result.stdout.strip()
        else:
            val_result = None

        return elapsed, val_result

    def run_loader(self) -> Dict:
        results = dict()

        for query in self.queries:
            elapsed, validation_result = self.run_sql(query)
            if elapsed != -1:
                results[query] = {
                    "elapsed": elapsed,
                    "validation_result": validation_result
                }
        
        return results

def main():
    parser = argparse.ArgumentParser(description="Data loader for BerlinMOD benchmark")
    parser.add_argument("--benchmark", type=str, required=True, help="Name of the benchmark run")
    benchmark = parser.parse_args().benchmark

    duckdb_path = "../../build/release/duckdb"
    if not os.path.exists(duckdb_path):
        print(f"Error: DuckDB executable not found at {duckdb_path}")
        print("Please make sure you're running this from the benchmark directory and DuckDB is built.")
        sys.exit(1)
    
    loader = DataLoader(duckdb_path, benchmark)
    results = loader.run_loader()
    
    if not os.path.exists(f"./results/stats/{benchmark}"):
        os.makedirs(f"./results/stats/{benchmark}")
    
    with open(f"./results/stats/{benchmark}/load_data.json", "w") as f:
        json.dump(results, f, indent=4)

    print(f"\nResults saved to ./results/stats/{benchmark}/load_data.json")

if __name__ == "__main__":
    main()