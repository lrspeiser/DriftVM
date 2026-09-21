# Cambrian-0.2: reusable blocks and a human-readable laboratory

## Run it on Windows

From the existing repository in PowerShell:

```powershell
git pull --ff-only
if ($LASTEXITCODE -ne 0) { throw "Git update failed" }
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run_lab.ps1 -Births 10000000 -Seed 1
```

This builds a **new, separate executable** (`driftvm_lab`), runs its regression tests,
starts evolution, and opens a browser on an automatically selected loopback port.
The original `driftvm` executable, Cambrian-0.1 code, launcher, and result folders remain intact.
CMake, a C++20 compiler, and Python 3.9+ are needed. No Python packages, Node, GPU,
LLM API, database, cloud deployment, or internet connection are needed to run the lab.
Node is optional for the developer cross-interpreter test.

Keep the launcher terminal open. After evolution completes, it continues serving the saved results.
**Ctrl+C closes the viewer and stops an unfinished experiment.** The already written files remain,
but there is no resumable checkpoint. Closing only the browser does not stop the engine.
`Pause view` freezes visualization, not evolution.

To inspect an existing run without evolving anything:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run_lab.ps1 -View "out\cambrian01-seed-1-20260920-222213-261-95605b"
```

The reader supports Cambrian-0.1 snapshots/discoveries and the new format. It does not
retroactively add blocks or parent witnesses to old runs. The case-study tab is a separate,
clearly labeled record of the user-supplied MAX-to-MIN transition.

Linux/macOS, or a manual build:

```bash
cmake -S lab -B build-lab -DCMAKE_BUILD_TYPE=Release
cmake --build build-lab --parallel
ctest --test-dir build-lab --output-on-failure
python3 scripts/lab.py --run out/new-lab-run --launch build-lab/driftvm_lab --births 10000000 --seed 1 --open
```

## What changed in the evolutionary substrate

An organism now carries a top-level program, its original 32-opcode dialect, and up to
16 inherited blocks. Each block contains 2–8 references to primitive opcodes or older
blocks. A rare **factoring mutation** moves a contiguous program sequence into a new
block and replaces that occurrence with a call. Later mutations can duplicate or insert
calls, modify a shared block body, and build blocks containing older blocks.

For example, `4 12 7 4 12 7` can be represented as `M0 M0`, where `M0 = 4 12 7`.
The experiment does not seed useful bodies or reward block count. A block's body is
copied from code actually present in its parent's genome. Our factoring/reuse machinery
is hand-designed; an observed block is not an independently invented abstraction mechanism.
A 1% mutation-mode budget is allocated to factoring/body changes; primitive dialect
changes retain the 0.3% probability. Ordinary mutation uses replacement, insertion,
deletion, or token duplication. This is a new experiment, not the same old seed trajectory.

### No free compute; no new primitive capability

Before evaluation, the blocks expand into the **unchanged Cambrian-0.1 VM**. The fully
expanded stream is limited to 96 primitive instructions, just like the baseline. A skip
can cross a block boundary, and `LOAD_IMM` sees the primitive opcode number. Factoring
therefore preserves exact semantics, including conditional skips and emissions.
The VM still executes four micro-operations per visited primitive instruction; a block
call is not counted as one unit of execution. Max nesting is 8, and references are acyclic.
Oversized/invalid children are rejected and counted as attempted births. Unreferenced
blocks are pruned and surviving references renumbered deterministically.

This changes the **search representation and mutational relationships**, not the set of
bounded computations the VM can express. Block depth, source compression, and repeated
references are structural descriptors, not proof of better algorithms, runtime speed,
intelligence, useful abstraction, or open-ended innovation. Counts in the UI describe
static expansion; input-dependent skips can prevent a reference from executing.

### Selection and evidence

Performance, novelty, and protected random-replacement pools retain separate criteria.
Performance uses current carrier counts; no stored historical scores are compared.
Each child is evaluated on 32 training and 128 screening inputs. First-discovery candidates
and final candidate survivors are exhaustively verified on all 65,536 byte-input pairs.
Sample-only false positives can still earn selection rewards; certification is an observer,
not a reward and not a guarantee covering every descendant.

For each first certified task, the system now also saves its immediate parent's full
specimen and exhaustively checks that parent against all ten named tasks. This exposes
transitions such as a correct MAX parent producing a correct MIN child without replaying
millions of ancestors. A discovery can be rejected by selection; its witness states this.
Parent/child ancestry does not by itself establish that neutral drift was necessary.

## What humans can see

The **living-population view** shows one tile per living program in its actual selection
pool. Task colors represent screened candidates; dots indicate inherited blocks. Clicking
any tile freezes that exact specimen for inspection while evolution continues.

The **discovery feed** contains first certified specimens, not repeated candidate matches.
The chart shows cumulative verified task types against attempted births. Flat periods are
not hidden. The feed and population are sampled at completed snapshots (default every
10,000 births), not animated as if every on-screen tile were one newborn.

The **microscope** accepts two byte inputs, runs the actual program, and steps through
visited micro-operations. It shows register values in decimal and binary, memory operations,
conditional skips, provisional emissions, block ancestry, and the complete executed trace.
The independent JavaScript interpreter is cross-checked against C++ with 2,000 generated
cases. The C++ exhaustive verifier remains authoritative. One interactive example is not a proof.

The **MAX-to-MIN case study** includes the supplied 95-instruction MIN specimen, the
94-instruction parent reconstructed by reversing `I 70 9`, and the independently verified
human-reduced 9/8-instruction versions. All four are checked in regression tests. These
are teaching/test fixtures only: none enter founder generation or evolution.
The shorter forms are labeled post-hoc human reductions, never evolved discoveries.

## Persistence, isolation, and controls

`state-N.json` files are immutable complete snapshots; only the newest two are retained for
the live view. `progress.csv` retains the time series. `lineage-modules.tsv` records every
admitted offspring and all founders. Ordinary children inherit dialect/blocks and record
an exact top-level sequence; dialect/block changes store the complete genome. This can
still consume substantial disk space on ten-million-birth runs. No claim of tiny storage
or automatic resumption is made. Keep the directory together.

`discoveries/TASK.genome` and `.json` preserve first-certified specimens and parent witnesses.
`final-verification.csv` checks every final task candidate. `config.txt` records the source
revision, seeds, mode flags, mutation rates, and execution limits.

Reconstruct an admitted ancestor without loading the entire history into memory:

```powershell
python scripts/lab.py --replay out/YOUR-RUN/lineage-modules.tsv --id 12345 --save ancestor.genome
.\build-lab\Release\driftvm_lab.exe --inspect ancestor.genome
```

Use an actual ID. Replay refuses an existing destination. The ID may be extinct but must
be admitted; rejected discovery children are saved separately in `discoveries/`.

The web server is read-only, bound to 127.0.0.1, and serves an explicit route allowlist.
There are no browser endpoints for arbitrary commands, filesystem writes, or interpreter
native-code execution. Neither browser input nor screen refresh changes evolutionary selection.

Compare modules and no-modules **inside version 0.2**, across multiple seeds and matched
birth/execution budgets; do not use the old run to isolate this one mechanism:

```powershell
.\scripts\run_lab.ps1 -Births 1000000 -Seed 2
.\scripts\run_lab.ps1 -Births 1000000 -Seed 2 -NoModules
.\scripts\run_lab.ps1 -Births 1000000 -Seed 2 -NoDrift
.\scripts\run_lab.ps1 -Births 1000000 -Seed 2 -NoModules -NoDrift
```

Each command runs in its own terminal or sequentially after closing the prior viewer.
NoDrift removes protected random-replacement slots, not all stochastic drift. Removing
modules changes which edits are possible; same seed does not imply identical trajectories.
There is still a fixed ten-task resource suite, not evolving tasks, unrestricted language
evolution, a self-replicating ecology, or LLM training. Those stronger claims are not made here.
