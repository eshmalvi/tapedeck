# Market data

Nothing in this directory except this file is committed. Real exchange data is
Nasdaq's property and far too large for git.

## Source

Nasdaq publishes complete TotalView-ITCH 5.0 trading days, free, at
https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/ (3-17 GB gzipped each). The files
are in Nasdaq's "BinaryFILE" layout: a sequence of frames, each a 2-byte
big-endian length followed by the message payload, terminated by a zero
length.

Protocol specification:
https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification_5.0.pdf

## Fetching a prefix

```bash
scripts/fetch_sample.sh 50            # first 50 MB (compressed) of 2019-01-30
scripts/fetch_sample.sh 150 12302019  # first 150 MB of 2019-12-30
```

gzip is a stream format, so a byte-range prefix of the archive inflates to a
valid prefix of the session: real messages from the start of day, simply cut
off at some point. The frame splitter drops the final partial frame.

| compressed prefix | inflated | covers (2019-01-30) |
|---|---|---|
| 50 MB | ~124 MB, 4.48 M messages | 03:04 to 07:29 ET (pre-market) |
| 150 MB | ~410 MB, ~12.5 M messages | through the 09:30 opening cross |
| full 4.76 GB | ~11 GB, ~300 M messages | the whole session |

## Files produced

- `<day>.prefix.gz`: the raw byte-range download. Readable directly by every
  tool (`GzipSource` streams it through zlib).
- `<day>.prefix.itch`: the inflated bytes. Readable via `mmap` (`MmapSource`),
  which is the fastest path.
