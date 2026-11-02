# GitDLer 2.2

A controller-first GitHub release downloader for Nintendo Switch homebrew.

## Workflow and controls

1. Add a GitHub URL or `owner/repository` with **+**, or a direct HTTPS file URL with **Y**.
2. Open a repository with **A** to browse **all its published releases**, including prereleases. Titles and tags are displayed side by side. The list is paginated (20 releases per server page); **L/R** load previous/next pages, and **ZL/ZR** move six rows within a page. There is no fixed history cutoff. **[LATEST]** marks GitHub's designated latest release, **[PRE]** marks prereleases, and **[DOWNLOADED]** marks your last completed version.
3. Use **A** on a release to read its full notes, or **X** to go straight to its files. The notes reader wraps Unicode text, distinguishes headings/code blocks, retains links as readable text, and scrolls with Up/Down or **ZL/ZR**. **Y** changes text size, **X** returns to the top, **A** opens files, and **B** goes back. It is a local text reader, not a full HTML/Markdown browser; links and images are not opened or fetched.
4. In the file list, **A** toggles a file, **Y** selects/clears all, **L** selects only `.nro` files, **R** reopens the notes, and **X** continues. Single-file releases are selected automatically. Revisiting the same release keeps your selection; opening a different release resets it to that release's defaults.
5. Pick a destination. **A** opens folders, **L** goes to the parent, **+** creates a folder, **Y** opens recent destinations, and **X** opens the review. Each repository remembers its last destination.
6. Review filenames and conflicts. **X** downloads and replaces existing files; **Y** skips existing files. The app checks available SD space against the known sizes before starting; files with unknown sizes can still run out of space during transfer.
7. **A** pauses/resumes downloads. **B** opens a cancel confirmation and pauses the active transfer when possible; **A** confirms cancellation, while **B** returns to the previous pause state. Completed files are kept. **X** in results retries failed/cancelled files from the beginning.

8. After a queue finishes, detected archives offer optional extraction. **A** chooses an extraction folder, **B** keeps that archive without extracting, and **Y** skips extraction for the rest. In the extraction review, **X** replaces existing files and **Y** skips them. The archive itself is always kept. From results, **Y** can extract (or retry extraction of) the selected successfully downloaded archive.

**B** returns to the previous workflow screen and restores its list position. Hold Up/Down or the left stick to scroll. From the library:

| Button | Action |
|---|---|
| A | Open release history / direct file |
| + | Add GitHub repository |
| Y | Add direct HTTPS link |
| L | Favorite/unfavorite the selected source; favorites sort first |
| R | Refresh the source and latest-release designation |
| X | Options: rename/remove a source, reduced motion, notes text size, exit |
| - | Exit |

In the release browser **+** refreshes the current server page and latest designation; **R** moves to the next page. The contextual footer always shows the current mapping. Reduced motion and note size are persisted in `preferences.json`.

## Archive extraction

Supported readers are ZIP, 7z, RAR/RAR5 and TAR, with gzip, bzip2, xz, zstd and LZ4 filters. Recognized names include `.zip`, `.7z`, `.rar`, `.tar`, `.tar.gz`, `.tgz`, `.tar.bz2`, `.tbz2`, `.tar.xz`, `.txz`, `.tar.zst`, `.tzst` and single compressed streams (`.gz`, `.bz2`, `.xz`, `.zst`, `.lz4`). Common signatures also detect archives downloaded without their usual extension. Password-protected archives, split volumes and compression methods unsupported by the linked libarchive build report an error.

- Choose any destination using the same folder picker, including new folders and recent download destinations. Archive folder structure is preserved. Extraction does not change a repository's remembered download destination.
- A worker thread inspects archive entries and known sizes before writing. The UI shows an inspection indicator, then a smoothed extraction percentage, current filename, saved/skipped counts, output bytes, speed and ETA when the total size is known. Unknown-size streams show bytes written and an indeterminate indicator.
- **A** pauses/resumes; **B** opens cancellation confirmation. Pause/cancel is applied between decoder operations, so a solid archive may take longer to respond while processing a block. The UI remains on the main thread.
- Unsafe absolute/traversal paths, symbolic/hard links and special files are rejected. Existing destination links are not followed. Each output file is staged and committed using the existing backup/rollback mechanism. The source archive cannot be overwritten by one of its entries.
- Cancellation/failure deletes the current partial file, keeps already completed extracted files, and preserves the archive. Extraction status appears separately in the download results; retry with **Y**, without downloading again.
- There is a 20,000-entry archive limit. Known unpacked sizes are checked against free space. Unknown-size streams can still run out of space. Decoder memory requirements vary; large solid 7z/RAR archives should be tested in full RAM mode.
- A forced shutdown may leave `.gitdler-extract-*.part` staging files or `.gitdler-backup` files. Inspect the destination before removing or restoring these files.

## Two layouts

Mode is detected automatically through libnx's applet type:

| | Applet mode | Full RAM mode |
|---|---|---|
| Layout | Full-width compact lists and transfer card | Lists with a release/destination/queue sidebar |
| Text cache | Up to 64 textures | Up to 192 textures |
| Transfer buffer | 16 KB | 64 KB |
| Release cache | Active repository page only | Most recently viewed page per repository |
| Features | Full file selection and queue controls | Same controls with persistent contextual detail |

Both layouts use a dark navy/teal palette, animated focus movement, eased screen slides/fades, smooth notes scrolling, frame-time-based progress smoothing, an indeterminate indicator when size is unknown, speed, estimated time remaining, and file position in the queue. No background bitmap is loaded. Rendering uses the existing SDL2/libnx stack; this is a native app, not a website.

## Storage and network behavior

- Config lives in `sdmc:/switch/GitDLer/`. Existing `repos.txt` and `history.txt` are read. Library edits are saved to `repos.json`; the legacy file is left intact.
- Release pages load on demand so startup does not wait for every repository. Page navigation follows GitHub pagination headers, with one page per repository retained. Public releases only; no GitHub authentication or draft access is configured.
- The latest badge comes from `/releases/latest`, not from list position or version-string sorting. A repository with only prereleases remains browsable even if there is no designated latest release. If the latest lookup fails, no guessed badge is shown. Failed page requests retain the previously loaded page.
- Metadata responses are limited to 4 MB per request; unusually large pages report an error rather than consuming unbounded memory.
- Files download sequentially through curl's multi interface while the UI keeps polling inputs. DNS resolution may briefly block on toolchains with a synchronous resolver.
- Downloads write to a unique `.gitdler-*` staging file. Cancellation or ordinary failure removes that partial file. Replacements move the old destination to `<filename>.gitdler-backup`, save the completed file, then remove the backup. A failed move attempts to restore the old file.
- A forced shutdown can leave staging files or a backup. Check the destination before manually removing staging files. If a `.gitdler-backup` remains, restore it to the original filename if needed; the app refuses to overwrite that backup. Library backup restoration is automatic when `repos.json` is missing.
- A downloaded version is recorded only when every selected file completes successfully. This indicates the selected files were downloaded, not that all release assets are installed.
- HTTPS verification remains enabled. For a curl build requiring an external CA bundle, place a trusted PEM bundle at `sdmc:/switch/GitDLer/cacert.pem`.
- There is no resume across app restarts or automatic installation. Retry restarts unfinished files. Replacement requires enough space for the new download alongside the existing file.

## Build

Use devkitPro with devkitA64, libnx, and the Switch SDL2, SDL2_ttf, curl and libarchive portlibs (plus their dependencies):

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=$DEVKITPRO/devkitA64
make -j2
```

Copy `GitDLer.nro` to your SD card's `switch/GitDLer/` directory. The same NRO selects its layout based on how it is launched.

Portable core tests (desktop C++17 compiler):

```sh
c++ -std=c++17 -Iinclude tests/core_tests.cpp source/model.cpp source/note_format.cpp -o core_tests
./core_tests
c++ -std=c++17 -Itests/stubs -Iinclude tests/workflow_tests.cpp source/state.cpp source/input.cpp source/model.cpp -o workflow_tests
./workflow_tests
```

The workflow tests execute the real input/state modules with mocked network/storage/platform services. Core tests cover parsing, pagination, latest identification, note wrapping, and file replacement/rollback.

Archive tests require a desktop libarchive development package (and Python for generated fixtures). Optional `bsdtar` creates additional 7z/zstd/LZ4 fixtures:

```sh
python tests/generate_archive_fixtures.py
c++ -std=c++17 -Iinclude tests/archive_tests.cpp source/archive_extract.cpp source/model.cpp -larchive -o archive_tests
./archive_tests build/archive-fixtures
```

The archive tests exercise real decoding, stored/deflated ZIP, compressed TAR and single streams, path/link rejection, CRC failures, replace/skip behavior, cancellation and source preservation. The old custom `source/miniz.h` is retained but is not used for extraction.

## Device validation

Before a release, test on a Switch in both Album/applet and title-override/full RAM mode:

- Add a repo with one file, several files, no assets, no release, and only prereleases; try offline and rate-limit responses.
- Browse several release pages; verify latest versus prerelease labels, older-version downloads, refresh failures, long notes, Unicode, code blocks, note text size, and back positions.
- Verify favorites, source rename, per-repository destinations, free-space warnings, and reduced motion after restarting.
- Select a subset, clear/select all, scroll beyond six rows, and cancel a release request.
- Download to an empty folder, create a folder, return to a recent folder, and check long/Unicode filenames.
- Pause/resume an active download; cancel both while active and paused; retry mixed success/failure results.
- Replace an existing file, skip existing files, run out of SD space, disconnect Wi-Fi, and verify original-file preservation.
- Verify progress and control responsiveness in both launch modes, and inspect SD files after exiting.
- Download mixed archive/non-archive queues. Test extract/keep/skip-all choices, selecting a different folder, extraction replace/skip, encrypted/corrupt archives, pause/cancel, low space and retry from results. Include RAR and large solid 7z files on hardware.

The native build and portable tests do not substitute for hardware validation.

## Third-party code

The release endpoints follow [GitHub?s release API documentation](https://docs.github.com/en/rest/releases/releases).

`include/json.hpp` is [nlohmann/json v3.11.3](https://github.com/nlohmann/json/releases/tag/v3.11.3), distributed under the MIT license in `licenses/nlohmann-json-MIT.txt`. SDL2, SDL2_ttf, libcurl, libarchive and libnx are supplied by devkitPro. Libarchive's license is included in `licenses/libarchive-COPYING.txt`. Existing artwork is retained in the repository but is not loaded by the redesigned UI.

## Source map

| File | Responsibility |
|---|---|
| `source/main.cpp` | Platform initialization, frame loop, shutdown |
| `include/app.hpp`, `source/state.cpp` | Shared state, navigation bookmarks, active release selection |
| `include/model.hpp`, `source/model.cpp` | Plain data structures, URL validation, GitHub JSON parsing, pagination detection |
| `source/storage.cpp` | Config migration/persistence, favorites ordering, folder listing, SD free space, keyboard |
| `source/network.cpp` | Private curl handles, release requests, staged downloads, pause/cancel/retry |
| `source/extraction.cpp` | Archive prompts, extraction worker lifecycle, progress UI |
| `include/archive_extract.hpp`, `source/archive_extract.cpp` | Portable streaming extraction engine, path validation and staged output |
| `source/input.cpp` | Contextual controller actions and workflow transitions |
| `include/ui.hpp`, `source/ui.cpp` | Palette, cached text, cards, scrollbars, common animation primitives |
| `source/screens.cpp` | Library, releases, files, folders, review, transfer and result layouts |
| `source/notes.cpp` | Notes reader layout, scrolling and font-size changes |
| `include/note_format.hpp`, `source/note_format.cpp` | Platform-independent UTF-8 text wrapping |
| `include/file_ops.hpp` | Staged replacement with backup and rollback |

`model.hpp` does not include the JSON library; only parsing/storage modules use it. The Makefile automatically compiles `source/*.cpp`, so new modules need no source-list update. Keep platform code out of model/note-format helpers to preserve desktop testability. The UI and network run on the main thread: only the network module owns curl handles, requests are pumped each frame, and repository mutations happen outside active requests. Extraction uses a dedicated libnx worker with atomic pause/cancel controls and mutex-protected progress snapshots. The worker never reads or mutates repositories, jobs or UI objects. Download selection is a working copy of the explicitly chosen release; the library's latest designation is stored separately.
