#!/usr/bin/env bash
# Downloads the first N megabytes of a real Nasdaq TotalView-ITCH 5.0 trading
# day and inflates it.
#
# Nasdaq publishes full days for free (3-17 GB gzipped each) at
# https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/. gzip is a stream format, so a
# byte-range prefix of the archive decodes to a valid prefix of the session:
# real messages from the 04:00 start of day, simply cut off at some point.
# ~50 MB compressed covers roughly the pre-market session; ~150 MB reaches the
# 09:30 open.
#
#   scripts/fetch_sample.sh            # 50 MB of 2019-01-30
#   scripts/fetch_sample.sh 150        # 150 MB of 2019-01-30
#   scripts/fetch_sample.sh 150 12302019
#
# Output: data/<day>.prefix.gz and data/<day>.prefix.itch (both gitignored).
set -euo pipefail

MB="${1:-50}"
DAY="${2:-01302019}"
URL="https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/${DAY}.NASDAQ_ITCH50.gz"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="$ROOT/data"
GZ="$OUT_DIR/${DAY}.prefix.gz"
RAW="$OUT_DIR/${DAY}.prefix.itch"
LAST_BYTE=$((MB * 1024 * 1024 - 1))

mkdir -p "$OUT_DIR"
echo "downloading bytes 0-${LAST_BYTE} of ${URL}"
curl --fail --location --retry 3 --progress-bar --range "0-${LAST_BYTE}" --output "$GZ" "$URL"

echo "inflating (a warning about an unexpected end of file is expected for a prefix)"
# gzip exits non-zero on the truncated tail but has already written every complete block.
gzip -dc "$GZ" > "$RAW" 2>/dev/null || true

echo "wrote $RAW ($(du -h "$RAW" | cut -f1))"
echo "try: build/apps/tapedeck_replay $RAW --symbol AAPL"
