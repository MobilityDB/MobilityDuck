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
└── brussels
    ├── instants.csv
    ├── licences.csv
    ├── periods.csv
    ├── points.csv
    ├── regions.csv
    ├── trips.csv
    ├── trips_200k.csv
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
- The `--explain` flag can take value 0 or 1 (default: 0), signifying whether the normal queries should be run (0) or the `EXPLAIN ANALYZE` queries should be run (1)
    - `--explain=0`: the output will be in the form of CSV files, located under `results/output/[benchmark]`
    - `--explain=1`: the output will not be stored, rather, only the `explain analyze` stdout will be stored under `results/output/[benchmark]/explain`

For example, for the `brussels` benchmark:
```bash
python3 run_queries.py --benchmark brussels --explain 1
```

Once finished, some basic statistics (query times, numbers of rows) are recorded in `results/stats/[benchmark]/load_[queries/explain].json`.
