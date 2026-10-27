#!/usr/bin/env bash
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

# refresh-from-master.sh — refresh this binding's generated surface against the latest MEOS API,
# end to end, with one command:
#
#   tools/refresh-from-master.sh
#
# It runs the shared MobilityDB/MEOS-API refresh-binding.sh over this repo — deriving the catalog
# from the latest MobilityDB master and regenerating this binding's surface. The per-binding last
# leg is in tools/refresh.conf.
#
# All of refresh-binding.sh's options pass through, e.g.:
#   tools/refresh-from-master.sh --mdb ~/src/MobilityDB   # refresh against a local MobilityDB branch
#   tools/refresh-from-master.sh --skip-tests             # regenerate + build, skip the tests
#
# MEOSAPI=<path> uses an existing MEOS-API checkout (any branch); otherwise MEOS-API master is
# cloned into the work dir. WORK_DIR overrides the scratch location (default <repo>/.meos-chain).
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${WORK_DIR:-$HERE/.meos-chain}"
MEOSAPI="${MEOSAPI:-}"

if [ -z "$MEOSAPI" ]; then
  MEOSAPI="$WORK/MEOS-API"
  mkdir -p "$WORK"
  if [ -d "$MEOSAPI/.git" ]; then
    git -C "$MEOSAPI" fetch --quiet https://github.com/MobilityDB/MEOS-API master
    git -C "$MEOSAPI" checkout --quiet FETCH_HEAD
  else
    git clone --quiet https://github.com/MobilityDB/MEOS-API "$MEOSAPI"
  fi
fi

exec "$MEOSAPI/tools/refresh-binding.sh" \
  --binding "$HERE" --meos-api "$MEOSAPI" --work-dir "$WORK" "$@"
