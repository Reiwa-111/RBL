# last verified: v0.8.0 | 2026.11.10 | IDE RESOURCE AUDIT

# RBL Studio — memory and CPU budget

## Historical egui frontend measurements — not wx evidence

The measurements and limits in this historical section describe the retired
Rust/egui frontend (`target/.../rbl-studio.exe`) only. They must not be used as
resource evidence for the primary wxWidgets frontend. Current wx acceptance is
recorded in the section below.

## Legacy target and safeguards

The desktop IDE targets a **512 MiB observed working-set ceiling** for normal
editing workloads. This is an engineering budget, not an operating-system hard
limit: graphics drivers, platform windowing code and external processes are not
fully controlled by the application. The measurement scripts fail when a
sampled peak exceeds the configured budget.

The editor and background work are bounded as follows:

| Resource | Bound |
|---|---:|
| Source/saved file bytes | 16 MiB per document |
| Decoded text | 16 MiB UTF-8 representation for all supported encodings |
| Aggregate open-document buffers, page history and Undo/Redo history | 64 MiB |
| Open tabs | 32 |
| Large-document UI window | 64 KiB of source text at a time |
| Project tree | 20,000 entries |
| Project search | Up to 20,000 discovered files, 16 MiB per file, 256 MiB of source bytes and 500 results |
| One file read by project search | 16 MiB |
| Output buffer | 1 MiB per panel, three panels |
| Background process output queue | 256 bounded messages |

Large documents are edited in independently navigable 64 KiB windows. Search,
Go to Line and diagnostics can move the visible window to a match, including a
match that crosses a nominal window boundary. The project-search worker checks
cancellation between 64 KiB chunks of long lines instead of lowercasing an
entire multi-megabyte line in one allocation.

## Windows measurements

Release executable: `target/x86_64-pc-windows-gnu/release/rbl-studio.exe`.
Measured 2026-11-09 on Windows 11 (build 26200.0), 12 logical processors.
Each run used a 5-second warm-up and 12 one-second samples (11.3 seconds
between the first and final sample). CPU is normalized to all logical processors.
All three runs used executable SHA-256
`E52FEB7C402C1BC8B1AF8E9661A3773AD819DB34BAC98C4663F709E57F5D8B7A` and
IDE source-set fingerprint at measurement
`5965555F12833A95A2AFF8498456F88998DF1034BEF8DC544B74129DA62CF3FD`.

| Workload | Working set average | Working set peak | Private bytes average | CPU total |
|---|---:|---:|---:|---:|
| Idle editor | 131.7 MiB | 131.9 MiB | 196.3 MiB | 0.00% observed |
| 16 MiB single-line `.txt` open | 172.5 MiB | 172.8 MiB | 235.7 MiB | 0.00% observed |
| 16 MiB single-line `.rbl` open (highlighting disabled above 1 MiB) | 172.9 MiB | 173.2 MiB | 236.2 MiB | 0.00% observed |

The 0.00% CPU readings mean no process CPU time was measurable at the one-second
sampling resolution during those windows. They are not a promise of zero wakeups
or zero CPU use during editing, search, compilation or rendering.

An earlier pre-optimization build measured 208.2 MiB average working set,
215.8 MiB peak, 250.4 MiB private bytes and 8.38% of total machine CPU capacity
while idle. The final build uses event-driven repainting while idle, caches the
project file list, virtualizes the tree and avoids relaying out the full source
of large documents.

## Linux / WSLg measurements

Release executable: `target/release/rbl-studio`. Measured 2026-11-09 in Ubuntu
24.04 / WSL2 (kernel `6.6.87.2-microsoft-standard-WSL2`, glibc 2.39), with 12
logical processors. Each run used a 5-second warm-up and 12 one-second samples
(11.0 seconds between the first and final sample). All runs used executable
SHA-256 `0de977061d0defac473f610c4080032a9bdd63633ca3dbb0413d55f9fd7e3688` and
IDE source-set fingerprint at measurement
`9193dbc56ca33fa65d9c808dcc8e158b824edfb721ecdf525ab92383fd7d00ab`.

WSLg emitted Mesa EGL/Zink device warnings during startup; the GUI process
nevertheless remained alive and all resource samples completed successfully.

| Workload | RSS average / peak | VmData average* | CPU total |
|---|---:|---:|---:|
| Idle editor | 131.3 MiB / 131.3 MiB | 274.5 MiB | 0.00% observed |
| 16 MiB single-line `.txt` open | 176.8 MiB / 176.8 MiB | 322.4 MiB | 0.00% observed |
| 16 MiB single-line `.rbl` open (highlighting disabled above 1 MiB) | 174.6 MiB / 174.6 MiB | 321.5 MiB | 0.00% observed |

*Linux `VmData` is an approximate data-segment measure, not the same metric as
Windows private bytes. RSS is the more useful cross-platform working-set
comparison. CPU readings have the same one-second sampling limitation.

## Extended workload coverage (2026-11-09)

The measurement harness has since been extended with named GUI workloads for
32 tabs, repeated edit/undo/redo across large documents, Find/Replace All,
compiler tasks and the project test suite. It records IDE and descendant-process
memory/CPU separately where the platform exposes those counters, and checks
that automation changed the expected files or spawned a child process.

The 32-tab scenario has now been measured on Windows using the current release
executable SHA-256 `12A06C6901AFCC33A2865309675A6CA0F18776CEC505F55EC0724B5B2E385676`
and IDE source fingerprint `01FD331064C32C0D19D1B644CDECBE45953C56AB7E145318B0A4873FEDA7FC9A`.
It opened 32 text documents of 1 MiB each (32 MiB source total), sampled for
15.2 seconds (33 samples), and stayed below the 512 MiB budget:

| Metric | Result |
|---|---:|
| IDE working set average / peak | 193.3 / 194.7 MiB |
| IDE private bytes average | 251.4 MiB |
| IDE + descendants working set peak | 194.7 MiB |
| IDE + descendants private bytes peak | 254.6 MiB |
| Process count | 1 |

The sample interval averaged 462.6 ms despite the requested 250 ms because
Windows process-tree sampling itself is relatively expensive. Treat this as a
steady-state memory observation, not a precise latency/CPU benchmark.

The edit/undo/redo and Find/Replace automation still fail to prove that their
keyboard actions changed the fixtures. The automation now verifies that the IDE
is the actual foreground window before sending input and preserves fixture/log
files on failure for diagnosis. Do not interpret a successful IDE launch or an
unchanged fixture as a workload pass. Linux/X11 automation and the revised
process-tree sampler also require a fresh end-to-end run before their output can
be used as a release measurement.

| Required workload | Accepted current result | Status |
|---|---|---|
| Empty/idle launch | Windows and WSL idle samples above | Measured |
| Ordinary document | No separately identified ordinary-size fixture in the retained measurement record | Not measured |
| 16 MiB text document | Windows and WSL samples above | Measured |
| 32 simultaneously open tabs | 32 × 1 MiB, 194.7 MiB process-tree peak | Measured |
| Sustained editing of several large documents | GUI automation did not verify edits | Blocked / not measured |
| Mass Find/Replace and Undo/Redo | GUI automation did not verify replacement and history operations | Blocked / not measured |
| Active compiler and descendant processes | No accepted process-tree sample | Not measured |
| Peak and steady-state IDE + descendants | No accepted combined workload sample | Not measured |
| CPU under editing/search/compiler load | No accepted active-workload sample | Not measured |

Consequently, the 512 MiB objective is **not yet demonstrated for the complete
application under the required heavy workloads**. Existing idle and 16 MiB
single-document observations remain valid only for those exact scenarios.

## wxWidgets frontend — current resource acceptance (2026-11-10)

The primary frontend is the C++20/wxWidgets 3.2/Scintilla application in
`native/wx-ui/`, with a separate Rust worker. Its document limits are 32 tabs,
16 MiB per document and 64 MiB aggregate UTF-8 editor text. The project tree is
bounded to 1,500 entries per directory and 50,000 nodes globally. Find in Files
is bounded to 100,000 entries, 128 MiB total scanned data, 2 MiB per file,
10,000 results and recursion depth 32. These are application-level bounds, not
a substitute for measuring the complete GUI + worker + child-process tree.

The wx resource harness is `benchmarks/wx_resource_acceptance.py`. It creates
31 text documents of exactly 2 MiB each (62 MiB) plus one small RBL task
document, opens all 32 in the real GUI, verifies each Scintilla document length
against the original or EOL-normalized fixture size and checks the aggregate
64 MiB bound, performs Scintilla append/undo/redo operations,
dispatches Check, Build, Run and Tests, samples the full process tree once per
second, then closes the documents and records a post-close footprint. It records
GUI and process-tree Working Set/private bytes, process count, handles, GDI/USER
objects, task state and UI responsiveness. The current acceptance limits are
512 MiB sampled process-tree Working Set, 512 MiB sampled process-tree private
bytes and 500 ms maximum UI ping. The ping timeout is deliberately longer than
the acceptance threshold, so a timed-out window message cannot be recorded as
a deceptively healthy sub-threshold sample.

The harness was strengthened on 2026-11-10 to verify that all 32 Scintilla
document controls actually opened, that append/undo/redo changed and restored
the expected document lengths, and that Close All Tabs leaves exactly one empty
new document. It also requires Check, Build, Run and Tests all
completed successfully. A still-running task, a non-zero task exit or missing
task coverage now fails acceptance. The preliminary 7,200-second run completed
under the earlier harness and is not current-RC evidence. A later 7,200-second
attempt failed early after task #45 without a CSV. A subsequent 480-second run
with the strengthened harness exited 1 after the initial Run Tests dispatch and
produced no CSV/verdict. No current-RC long-duration soak PASS is available;

Do not use the legacy `measure_ide_resources.ps1` / `.sh` scripts below as wx
acceptance evidence: they launch the old egui executable. Do not transfer the
old egui memory measurements above to wx.

## Reproducing legacy egui measurements

Windows PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File benchmarks/measure_ide_resources.ps1
powershell -ExecutionPolicy Bypass -File benchmarks/measure_ide_resources.ps1 `
  -DocumentPath C:\path\to\large.txt -WarmupSeconds 8 -SampleSeconds 20 `
  -MemoryLimitMiB 512
```

Linux / WSLg:

```bash
bash benchmarks/measure_ide_resources.sh
RBL_STUDIO_SAMPLES=20 RBL_STUDIO_MEMORY_LIMIT_MIB=512 \
  bash benchmarks/measure_ide_resources.sh /path/to/large.txt
```

Both scripts launch the release executable, wait for warm-up, sample process
memory and CPU, print average and peak memory, and stop the process afterward.
The Windows script can also write a CSV with `-CsvPath`. The extended GUI
scenarios require an interactive desktop session with permission to focus and
send input to the IDE; a headless session is not a valid substitute. Linux
scenarios require X11 automation support and a working X11/WSLg display.
Each legacy run prints the executable SHA-256 and an IDE source-set fingerprint
computed at measurement time from `src/main.rs`, `src/app.rs`, `src/editor.rs`,
`src/process.rs`, `src/settings.rs`, `Cargo.toml` and `Cargo.lock`. The latter is
not embedded in the executable and therefore does not prove which sources
produced that binary; retain the executable hash and build record together when
archiving measurements.

## Interpretation

- Working set / RSS is resident process memory, not the total memory reserved
  by the process or the amount of system RAM in use.
- Private bytes and VmData are not directly interchangeable.
- Peaks are sampled, so very short spikes between samples may be missed.
- The 512 MiB target is supported by explicit document, tab, tree, search,
  output and editor-layout limits, plus the sampled idle, single-large-file and
  32-tab memory measurements above. It is not a hard allocator quota enforced
  by the operating system.
- A 32-tab GUI session (32 × 1 MiB text documents) has been sampled separately:
  194.7 MiB IDE working-set peak and 254.6 MiB IDE-plus-descendants private-byte
  peak, with one process present. This is a memory observation only; the run did
  not verify sustained editing, Find/Replace/Undo or active compiler workloads.
- A multi-tab aggregate-budget regression test verifies that text buffers and
  page-history allocations are counted across tabs.
- A running compiler task retains an in-memory source snapshot for unsaved
  documents. Memory while a task is active, including child-process memory, has
  not been measured as part of the IDE-process figures above.
- The expanded multi-workload measurement scripts are experimental until the
  scenario's own verification checks pass and the resulting CSV/log is retained.
- Active editing, huge paste operations, large project search and compiler
  workloads should be benchmarked separately when setting formal latency or
  CPU targets.

## Separate wxWidgets frontend observations — 2026-11-10

These measurements apply to `native/wx-ui/` (wxWidgets 3.2 + Scintilla + the
Rust worker), not to the Rust/egui executable measured above. They were captured
before the 2026-11-10 EOL and search-close follow-up patches; the retained
measurement record does not contain the exact executable hash. Do not attribute
them to the current rebuilt executable without repeating the workload.

| Workload | GUI Working Set / Private Bytes | Worker Working Set / Private Bytes | Process-set observation |
|---|---:|---:|---:|
| 32 tabs × 2 MiB (64 MiB source total) | ~175 / ~150 MB | ~10 / ~3 MB | ~185 MB WS / ~153 MB private |
| Same session after Close All Tabs | ~37 / ~11 MB | ~10 / ~3 MB | ~47.5 MB WS |
| One exact 16 MiB document | ~69 / ~44 MB | ~10 / ~3 MB | ~79 MB WS / ~47 MB private |
| ~20 MiB console output, after task completion | Not retained separately | Not retained separately | ~78 MB WS / ~46 MB private |

The 32-tab session remained responsive, and GUI memory fell substantially after
closing all documents. The exact 16 MiB file was visibly opened in Scintilla.
The output stress task completed with exit code 0 and the GUI remained
responsive. These are individual observed samples, not continuous peak traces;
the earlier runs did not retain a reliable CPU time series or a compiler-child
process-tree sample. No long-duration leak profile was accepted. The 512 MiB
whole-application target therefore remains unproven for sustained editing,
active compiler/tests and long-running searches.

The final Windows wx build after the 2026-11-10 EOL, search-close, Save
revalidation and worker stream-reader changes is identified by GUI SHA-256
`3A28F6BCA56317D5E7B391C1803328B8B7375000A9A7724ED02C7C86AB7D8C02` and worker
SHA-256
`3A8921D8503AB9AF53A88D732BD1FC830A084DF3890AF9DDEFEB6F299361405C`.
Those hashes identify the final build, not the earlier resource samples.

## wxWidgets frontend sustained resource sample — 2026-11-10

Ran `python benchmarks/wx_resource_acceptance.py --duration 180 --interval 1.0`
against the current wx executable above. The measured session lasted 200.4 s
with 195 process-tree samples. It opened 32 documents (31 × 2 MiB text files
plus one RBL task file; 64 MiB total), performed eight Scintilla append/undo/redo
cycles, started the IDE test task, then closed all tabs and sampled the reduced
footprint for a further 20 s. The sampler included the GUI, worker and all
observed descendants.

| Metric | Result |
|---|---:|
| GUI working-set mean / peak | 176.7 / 192.1 MiB |
| GUI private-bytes mean / peak | 154.3 / 170.1 MiB |
| Process-tree working-set mean / peak | 200.4 / 260.0 MiB |
| Process-tree private-bytes mean / peak | 160.4 / 191.3 MiB |
| Maximum observed process count | 7 |
| Process-tree CPU-time delta over sample | ~5.6 s |
| Final process-tree working set after Close All Tabs | 60.0 MiB |
| Final process-tree private bytes after Close All Tabs | 16.7 MiB |

The sampled peak stayed below 512 MiB for both working set and private bytes.
The GUI alone fell to 37.4 MiB working set / 11.3 MiB private bytes after the
documents were closed. The retained CSV is
`build/wx-ui-win/resource-acceptance/wx-resource-acceptance.csv` (build output,
not a tracked artifact). This is meaningful sustained multi-document and
edit-history evidence for this executable, but it is not a worst-case upper
bound: it does not include long-running Find in Files, large diagnostic result
sets, or a sustained compiler child process that continuously allocates memory.

## wxWidgets frontend extended acceptance — 2026-11-10

After the follow-up fixes, the Windows wx executable was rebuilt and measured
with `python benchmarks/wx_resource_acceptance.py --duration 300 --interval 1.0`.
The run lasted 319.5 s and retained 313 process-tree samples. It opened 32
documents (64 MiB total), exercised Scintilla append/undo/redo, dispatched the
actual Run Tests command (status changed from Ready to Starting tests), closed
all tabs and continued sampling the reduced footprint. The command ID was
verified against `ID_Tests` and the worker task mapping to `tests/run_tests.py`.

| Metric | Result |
|---|---:|
| GUI working-set mean / peak | 176.8 / 192.2 MiB |
| GUI private-bytes mean / peak | 154.7 / 169.9 MiB |
| Process-tree working-set mean / peak | 201.1 / 257.2 MiB |
| Process-tree private-bytes mean / peak | 165.8 / 191.1 MiB |
| Maximum observed process count | 7 |
| Process-tree CPU-time delta | 2.14 s |
| Final process-tree working set after Close All Tabs | 57.6 MiB |
| Final process-tree private bytes after Close All Tabs | 17.7 MiB |

The sampled process-tree peak remained below 512 MiB. The test task's descendant
processes were present in the process tree, but only during a short portion of
the five-minute run; this is not a five-minute continuous compiler stress test.
The retained CSV is `build/wx-ui-win/resource-acceptance/wx-resource-acceptance.csv`.
This run used GUI SHA-256
`3735FC7C08608E464DD0313C586BE82E7C39F0625AA6826A66C1E2E61F501AE2` and worker
SHA-256 `3A8921D8503AB9AF53A88D732BD1FC830A084DF3890AF9DDEFEB6F299361405C`.

A separate project-tree stress run created 34 directories with 1,500 files each,
expanded them through the real wx tree control, and reached exactly 50,000 tree
nodes without exceeding the global cap. The process-tree peak during expansion
was 276.3 MiB RSS / 224.7 MiB USS. A separate 1,501-entry single-directory test
confirmed the 1,500-entry cap plus its truncation marker (1,502 total tree nodes).

Follow-up directory-heavy stress used 35 parent directories with 1,500 child
directories each. Each child directory contributes both a visible node and a
lazy `...` placeholder. After correcting the global node accounting to charge
two nodes per directory and reserve the truncation marker, the real tree reached
49,999 nodes (the remaining single slot cannot fit another directory/placeholder
pair) and did not exceed the 50,000-node cap. Peak process-tree RSS was 88.5 MiB.

Find in Files was separately stressed with 12,000 candidate hits. The UI applied
the 10,000-result cap and populated the result list in 50-row timer batches. The
latest isolated rerun completed in 3.72 s, with 76.9 MiB process-tree RSS / 29.2
MiB USS peak and 92.0 ms maximum measured `WM_NULL` UI response. An earlier
unloaded run measured 6.16 s / 76.4 MiB RSS / 28.7 MiB USS / 264.7 ms. A combined
GUI acceptance batch under concurrent test load briefly exceeded the test's
1-second responsiveness timeout; the isolated rerun passed. A previous 200-row
batching trial exceeded 500 ms on a slower sample; the final 50-row batch is the
configuration measured here.

The current wx frontend also passed GUI cancellation tests: Stop disables stdin,
Send and EOF immediately; two consecutive infinite-loop Runs were each cancelled
with exit code 130 while the same worker process remained alive. A separate GUI
test queued a line and immediate EOF; the child persisted the line before
observing EOF and exited through the expected EOF path.

## Current rebuilt wx frontend sample — 2026-11-10

After the project-tree accounting, batched search-result insertion, Stop input
gating and worker shutdown-drain fixes, the current Windows binaries were sampled
with `python benchmarks/wx_resource_acceptance.py --duration 180 --interval 1.0`.
The run lasted 200.5 s and retained 196 samples. It opened 32 documents / 64 MiB,
exercised edit/undo/redo, dispatched the actual Tests command (status changed
from `Ready` to `Starting tests`) and closed all tabs.

| Metric | Mean | Peak | Final sample |
|---|---:|---:|---:|
| GUI working set | 176.4 MiB | 191.9 MiB | 34.8 MiB |
| GUI private bytes | 154.4 MiB | 170.1 MiB | 10.5 MiB |
| Process-tree working set | 200.3 MiB | 254.1 MiB | 57.3 MiB |
| Process-tree private bytes | 160.4 MiB | 191.0 MiB | 15.9 MiB |

Maximum process count was 7. The sample remained below 512 MiB. The process-tree
CPU-time delta was only 1.09 s; the Tests task dispatch was confirmed, but this
sample is not evidence of a sustained compiler stress workload. The CSV is
`build/wx-ui-win/resource-acceptance/wx-resource-acceptance.csv`.

Current measured binary hashes: GUI
`F84AEE828DD9E9F5A782C46623BF6B32E21FDBF06CA9D88FE763DA6F7E5665CB`, worker
`05B144E3AF0FE31D2D7E9696DED305F0AA9E1B7443D6BA642F84EDDE504790AE`.

## Sustained acceptance rerun — 2026-11-10

The same current binaries completed a 300 s requested run (`--duration 300
--interval 1.0`) in 319.9 s with 313 samples. The workload again opened 31 × 2 MiB
text documents plus the task document (32 tabs / 64 MiB), exercised editor
append/undo/redo, dispatched the actual IDE Tests command, then closed all tabs
and measured the post-close process tree. The test-task status was observed
transitioning from `Ready` to `Starting tests`; this sample does not claim a long
compiler stress phase.

| Metric | Mean | Peak | Final post-close |
|---|---:|---:|---:|
| GUI working set | 182.1 MiB | 191.9 MiB | 35.1 MiB |
| GUI private bytes | 160.0 MiB | 170.0 MiB | 10.6 MiB |
| Process-tree working set | 205.5 MiB | 263.0 MiB | 57.9 MiB |
| Process-tree private bytes | 165.8 MiB | 192.4 MiB | 16.2 MiB |

Maximum process count was 7. Process-tree CPU time increased by 1.69 s across
the sampling interval. The measured workload remained below 512 MiB. The raw
CSV was retained at `build/wx-ui-win/resource-acceptance/wx-resource-acceptance.csv`.

These observations support the 512 MiB target for the measured workloads; they
do not establish a hard allocator quota or a worst-case bound. Multi-hour leak
testing, continuous compiler/search stress, very large diagnostic lists and DPI
scaling remain outside this evidence.
