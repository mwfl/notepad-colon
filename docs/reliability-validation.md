# Reliability validation — 2026-11-15

Local Windows x64 validation for the reliability changes following `21bbea3`.
The [test guide](large-file-testing.md) documents commands, limits and workload.

## Scope

- Recovery: complete large-file checkpoints; empty dirty text; checksum and
  previous-generation fallback; preserved failed/partial startup sessions;
  bounded history worker, corruption/quota failure and retry; generated unsaved
  tabs keep their save prompt after undo back to initial text.
- Encoding/reload: shared lossless decoding, explicit code-page persistence,
  UTF-16 byte order/BOM, EditorConfig fallback, exact disk identity and save
  writer exclusion, UTF-8 follow across append/truncate/rotation/regrowth.
- Batch replacement: frozen selected output, disk/buffer stale-state rejection,
  durable partial progress, conflict-aware history restore and cancellation.
- Performance: insertion/deletion churn, bounded diagnostics, dense search result
  locations, cancellation inside backtracking regex matching, native resource
  monitoring, and complete 4 GiB streaming save/reopen verification.
- Interface: six complete language catalogs; native-menu hierarchy, checked
  language option, immediate switching and preference migration/persistence.

Debug and Release each passed all 12 CTest cases. The portable ZIP passed its
isolated application self-test with bundled runtimes, and MSIX creation passed
the Windows SDK packaging validation. Packages are local validation artifacts;
this work does not submit a new Microsoft Store release.

## Ten-minute native sustained run

Release workload with an append handle held open between rotations:

| Measurement | Result |
| --- | ---: |
| Duration | 600 seconds |
| Follow frames / advancing frames | 6,884 / 6,883 |
| Writer appends / rotations | 38,652 / 2 |
| Open-edit-undo-close cycles | 2,294 |
| Folder-search jobs started | 2,294 |
| Window heartbeat p99 / maximum | 154.787 / 292.380 ms |
| Missed one-second heartbeats | 0 |
| Private bytes, first / last large-file phase | 24,612,864 / 31,784,960 |
| Private-memory growth | 7,172,096 bytes |
| Kernel handle / GDI / USER growth | +5 / +1 / +1 |

The initial 45-second test exposed approximately 1.7 GiB of growth from retaining
every refreshed view in undo history. Installing explicit view baselines removed
that growth; subsequent 45-second Debug/Release runs and the extended run passed
the original resource/heartbeat limits.

The final generated-tab save-prompt adjustment was verified afterward by native
GUI and complete 45-second suite runs; the ten-minute workload exercises named
files, so it does not cover that new-tab behavior.

## Complete 4 GiB streaming save

The opt-in Release test saved modified first/middle/last regions, reopened the
result, and compared every byte with the independent fixture:

| Measurement | Result |
| --- | ---: |
| Full save | 5,862 ms |
| Full reopen/read verification | 5,636 ms |
| Sampled additional private memory | 8,466,432 bytes |
| Handle delta in the performance suite | 0 |

These timings describe this machine and workload. Whole-file saves and optional
auto-save remain synchronous; the heartbeat run does not establish responsiveness
during a 4 GiB save. Large-file undo is scoped to the current loaded window.
Longer sessions, other storage devices, ARM64 runtime behavior and broader
accessibility/input-method coverage require their own acceptance measurements.
