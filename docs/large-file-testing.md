# Large-file testing

The large-file path avoids placing the complete document in Scintilla or RAM.
An 8 MiB decoded window is backed by a file reader and a piece table. The editor
uses shared-delete buffered handles so log rotation does not have to wait for
an open memory mapping. Saving
streams original and inserted pieces to a temporary file, flushes it, checks
the source again for external changes, and atomically replaces the destination.

Run the automated checks with Visual Studio 2026 x64:

```powershell
cmake --preset vs2026-x64
cmake --build --preset vs2026-x64-release --parallel
ctest --preset vs2026-x64-release -L performance --output-on-failure
```

`notepad-colon.large-file-edit` creates a 34 MiB UTF-8 C++ text file, opens it
through the application, edits and checkpoints several distant regions, restores
the complete checkpoint, streams the save, and checks both the marker and
unchanged tail. It also follows append, shrink, atomic rotation, truncation to
zero and regrowth, and verifies that whole-file conversions cannot change just
the loaded window. `notepad-colon.performance` creates a sparse
file of exactly 4 GiB, opens it without allocating 4 GiB, edits near the tail,
and verifies fixed-memory search near that position.

Manual acceptance for a 100 MiB or 4 GiB UTF-8 file:

1. Measure launch-to-first-window and confirm memory stays near the window size.
2. Move through several windows and drag the scrollbar/caret without a freeze.
3. Insert, delete, undo, and redo near the beginning, middle, and end.
4. Search for text beyond the current window and confirm the matching window
   loads with the match selected.
5. Save As, compare size and hashes of unchanged ranges, then reopen.
6. Modify the source externally before saving and confirm Notepad:: refuses
   to overwrite it.
7. Enable **Follow Growing File**, append lines externally, and confirm the view
   advances without discarding edits (follow mode requires a clean document).
8. Rotate or truncate the followed file, including to zero bytes, then grow it
   again. Confirm the next valid update loads and following remains enabled.

Full-file regular expressions are deliberately restricted to the loaded window
for large files. Literal full-file search is streaming and bounded-memory.
Undo/redo applies to edits within the current large-file window; loading another
window starts a new undo baseline. All committed window edits remain in the
complete piece table and recovery checkpoint. Ordinary document editing and
reviewed replacements retain their normal undo behavior.

## Sustained native application test

CTest runs `notepad-colon.sustained-run` for 45 seconds in both configurations.
For ten-minute acceptance, run the same production callbacks and native controls:

```powershell
./tests/large_file_test.ps1 -Executable ./build/vs2026-x64/Release/notepad-colon.exe -SoakSeconds 600
```

The writer holds its append handle open between rotations. The application
follows the 34 MiB C++ file, rotates to an empty file, grows it, then replaces it
with a large file again. Every three frames it opens, edits, undoes and closes a
syntax-enabled document, queues recovery history, changes the interface language,
rebuilds menus and starts/cancels a real folder-search worker.

A separate thread sends native-window heartbeats every 10 ms. Acceptance requires
no missed one-second heartbeats and p99 response at most 250 ms in Release
(1,000 ms in Debug). At least 80% of sampled frames must show a newer writer
counter. Compare private bytes in the first and last large-file phases: growth
must stay below 64 MiB; kernel handles may grow by at most 24 and GDI/USER objects
by at most 32 each. The report prints actual counts, timings and resource deltas;
these are measurements on the test machine, not universal latency guarantees.

Automatic reloads and read-only previews install text without collecting undo.
Follow mode skips full syntax-tree rebuilds and external-change review snapshots,
while retaining lightweight syntax coloring. Each read uses its captured file
extent; later appends are displayed in the next frame. An incomplete final UTF-8
scalar rejects that frame and retains the previous valid view. Saving and recovery
still require exact source identity/state and exclude concurrent writers.

## Complete 4 GiB save

The default performance test uses sparse fixtures for fast CI. Opt in to writing
and rereading all 4 GiB with at least 5 GiB of available temporary disk space:

```powershell
$env:NOTEPAD_COLON_FULL_SAVE_TEST = '1'
try { ./build/vs2026-x64/Release/notepad_colon_performance_tests.exe }
finally { $env:NOTEPAD_COLON_FULL_SAVE_TEST = $null }
```

This modifies the beginning, middle and end, saves through the streaming path,
reopens, and compares every byte against an independent zero/marker fixture.
Sampled private-memory growth must remain below 64 MiB. JSON includes separate
save and verification timings; `-1` means the opt-in step was not requested.

Whole-file user saves and optional auto-save still run synchronously. The native
heartbeat acceptance workload exercises log following and ordinary editing, not
an interactive 4 GiB save. Moving long saves to a cancellable worker remains a
separate improvement; the streaming test proves content correctness and bounded
memory, not a responsive window during that save.

## Recovery and matching bounds

`notepad-colon.reliability` compares 10,000 random piece-table replacements with
an independent byte vector and churns 125 MiB of temporary insertions/deletions.
It checks snapshot independence, buffer release, corruption, quota admission,
background failure/retry, empty text, session archival/rollback, live append
reads and cancellation inside a single backtracking regex match.

Large-file insertion storage compacts when deleted bytes reach 1 MiB and exceed
the live inserted bytes. Ordinary recovery history has a 128 MiB pending-text
budget and a 256 MiB/1,000-record archive admission limit. Pruning retains two
loadable generations per document, even beyond the ordinary history count.
If protected records fill the budget, new history writes fail visibly until the
user reviews/deletes snapshots in Recovery Manager. Session checkpoints use their
separate complete-file recovery mechanism. Failed or partial startup sessions
move into `UnrestoredSessions` with their manifests and sidecars, retaining
relative references so later checkpoints cannot prune them.

Folder search/replacement and line-filter regex matching keep ECMAScript capture
and lookahead behavior, check cancellation inside the matcher, and have a
two-second per-text matching budget (per physical line for line filtering).
Timeout/complexity errors are reported explicitly and never applied as successful
replacement output or interpreted as a nonmatching inverted-filter line.
