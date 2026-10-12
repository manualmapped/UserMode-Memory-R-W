# Process transfer performance results

## Change measured

The implementation coalesces `read_many` calls only when every remote range and every destination buffer is adjacent. A fully successful combined read returns one complete result per input request. If the combined read fails or is partial, the method falls back to one read per range so each result preserves its existing independent failure semantics.

## Same-run, interleaved comparison

The current and repeated-read methods use the same GCC 15.2.0 process, fixture, and private `PAGE_READWRITE` buffer. Their order is shuffled within each of seven samples. This isolates the coalescing benefit from process startup and most run-to-run variance. Values below are median nanoseconds per range from `gcc15.2-win11-i5-12400f-private-rw-current.csv`.

| Pattern | Bytes/range | Ranges | Repeated ns/range | `read_many` ns/range | Speedup |
| --- | ---: | ---: | ---: | ---: | ---: |
| contiguous | 64 | 256 | 670.13 | 9.75 | 68.7x |
| contiguous | 4,096 | 256 | 1,229.36 | 263.80 | 4.7x |
| disjoint | 64 | 256 | 645.97 | 611.82 | 1.06x |

The largest gain is for many tiny adjacent ranges, where one OS transfer replaces hundreds. Disjoint ranges cannot use the coalesced path; their small difference reflects the single batch lock and measurement variance. These are per-range medians on one machine, not universal throughput guarantees. An additional macro-disabled binary run provides a historical-style check of the old `read_many` loop, but it is a separate-process comparison and is not used in the speedup claims.

## Current libraries

`msvc19.51-win11-i5-12400f-private-rw-current-competitors.csv` records the current implementation, direct Win32, libvas, and libmem on the same private-memory fixture, using MSVC 19.51.0 and seven samples. For a single 64-byte read, medians were 613 ns (`ProcessSession`), 574 ns (`ReadProcessMemory`), 574 ns (libvas), and 1,273 ns (libmem). This shows there is no single-read speedup claim. For contiguous 256-range batches, `ProcessSession` measured 9.54 ns/range at 64 bytes and 268.42 ns/range at 4 KiB, versus repeated reads at 645.93 ns/range and 1,176.38 ns/range, respectively.

The compared upstream heads as of 2026-10-11 are libvas `d3621d4c17b6ca9b9394f1af910c89ddafb3f6d7` and libmem `a07c9942bf1358dabcc83eb0cd072736c749d7f8`. Their APIs differ: libmem opens and closes a process handle per operation, while the other single-transfer paths retain handles. Blackbone was inspected at `5ede6ce50cd8ad34178bfa6cae05768ff6b3859b` but is not included in measured results.

## Historical project revisions

The same private-RW child fixture measured the old global Nt* wrappers at GCC 15.2.0, with setup excluded and seven samples. They predate `ProcessSession` and expose a different API and rights model, so treat them as context rather than a strict drop-in ranking. Median 64-byte reads were 528.50 ns at `307aad3cc410559f2808723fb835cb23b10157fc` and 526.65 ns at `38732bab803482a6ae17214fa236cfee36b91ea6`. The old typed-value API is stack-limited; sizes over 64 KiB are omitted.

## Environment and verification

- Windows build 26100, Intel Core i5-12400F, 12 logical processors, 4 KiB pages.
- GCC comparisons use GCC 15.2.0; current peer-library comparison uses MSVC 19.51.0.
- CMake Release build with libvas and libmem adapters succeeded; `process_session_integration` passed under MSVC.
- Current benchmark files report `validation=passed` or `validation=completed_on_valid_ranges`; p95 uses the nearest-rank definition (the maximum of seven samples at p95).
- Single-write timings are unusually high (roughly 47 microseconds for small writes) across `ProcessSession`, direct Win32, libvas, and libmem on this host. The benchmark records this as observed and makes no write-speed improvement claim.
