# Run BerlinMOD benchmark

This benchmark is based on [BerlinMOD for MobilityDB](https://github.com/MobilityDB/MobilityDB-BerlinMOD). The script available in this directory assumes the data has already been generated.

## 1. Locate test data
Before running the scripts, the CSV files from BerlinMOD need to be put in the correct location. From this root directory, under `data/`, create a sub-directory with the name of the benchmark city, e.g.:
```bash
cd data
mkdir brussels
```

Put the CSV files in this newly created directory. For example, after copying, `data/` should have the following structure:
```
data
└── hanoi_0.05
    ├── instants.csv
    ├── licences.csv
    ├── municipalities.csv
    ├── periods.csv
    ├── points.csv
    ├── regions.csv
    ├── roadsegments.csv
    ├── tripsinput.csv
    └── vehicles.csv
```

## 2. Load the data
Run the script to create tables and load CSV files into respective tables. The script has one required flag:
- The `--benchmark` flag should have the same name as the data directory.

For example, for the `brussels` benchmark:
```bash
python3 load_data.py --benchmark brussels
```

Once finished, some basic statistics (load times, numbers of rows) are recorded in `results/stats/[benchmark]/load_data.json`.

## 3. Run the queries
Run the script to run the benchmark queries. The script has one required flag and one optional one:
- The `--benchmark` flag should have the same name as the data directory.
- The `--type` flag can take value `native` or `accelerated` (default: `native`), signifying whether the normal queries should be run or the accelerated queries should be run

For example, for the `brussels` benchmark:
```bash
python3 run_queries.py --benchmark brussels --type native
```

Once finished, some basic statistics (query times, numbers of rows) are recorded in `results/stats/[benchmark]/load_[native/accelerated].json`.

## 4. Profile the results
Run the script to summarize the benchmark results in terms of number of rows returned and latency of each query. The script has the same two flags as the query runner.

For example, for the `brussels` benchmark:
```bash
python3 profile_results.py --benchmark brussels --type native
```

Once finished, the results are saved in a .csv file at `results/stats/[benchmark]/run_[native/accelerated]_profile.csv`.