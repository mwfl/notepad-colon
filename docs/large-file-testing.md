# Large-file testing

The large-file path avoids placing the complete document in Scintilla or RAM.
An 8 MiB decoded window is backed by a mapped source and a piece table. Saving
streams original and inserted pieces to a temporary file, flushes it, checks
the source again for external changes, and atomically replaces the destination.

Run the automated checks with Visual Studio 2026 x64:

```powershell
cmake --preset vs2026-x64
cmake --build --preset vs2026-x64-release --parallel
ctest --preset vs2026-x64-release -L performance --output-on-failure
```

`notepad-colon.large-file-edit` creates a 34 MiB UTF-8 C++ text file, opens it
through the application, inserts a marker, streams the save, and checks both
the marker and unchanged tail. `notepad-colon.performance` creates a sparse
file of exactly 4 GiB, opens it without allocating 4 GiB, edits near the tail,
and verifies fixed-memory search near that position.

Manual acceptance for a 100 MiB or 4 GiB UTF-8 file:

1. Measure launch-to-first-window and confirm memory stays near the window size.
2. Move through several windows and drag the scrollbar/caret without a freeze.
3. Insert, delete, undo, and redo near the beginning, middle, and end.
4. Search for text beyond the current window and confirm the matching window
   loads with the match selected.
5. Save As, compare size and hashes of unchanged ranges, then reopen.
6. Modify the source externally before saving and confirm Notepad Colon refuses
   to overwrite it.
7. Enable **Follow Growing File**, append lines externally, and confirm the view
   advances without discarding edits (follow mode requires a clean document).

Full-file regular expressions are deliberately restricted to the loaded window
for large files. Literal full-file search is streaming and bounded-memory.
