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