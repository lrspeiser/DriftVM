# DriftVM

An artificial-life experiment in executable computation. Programs mutate, their inherited instruction meanings can change, and computational tests determine what they actually do. The host is **C++20**, not an LLM. Organisms execute bounded custom bytecode, not native machine code.

## New: Cambrian-0.2 — reusable blocks + Evolution Lab

The next experiment lets programs **factor existing sequences into inherited blocks**, reuse them, mutate their bodies, and build blocks from earlier blocks. The browser shows actual living programs and verified discoveries, with an interactive microscope that steps through execution.

The original Cambrian-0.1 engine and result folders remain intact. The new experiment is a separate executable, `driftvm_lab`.

### Windows: update, build, test, run, watch

From your existing DriftVM folder in PowerShell:

```powershell
git pull --ff-only
if ($LASTEXITCODE -ne 0) { throw "Git update failed" }
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run_lab.ps1 -Births 10000000 -Seed 1
```

This builds Release in `build-lab`, runs the lab regression tests, starts ten million births, and opens your browser on an unused **local** port. It creates a new output folder and refuses to overwrite previous results. Git, CMake, a C++20 compiler and **Python 3.9+** are required. No Python packages, Node, GPU, API key or cloud service is needed to run the lab.

Keep the terminal open. Closing the browser does not stop evolution. **Ctrl+C closes the viewer and stops its unfinished child experiment.** Completed snapshots and ancestry remain on disk, but there is no resumable checkpoint. After evolution completes, the viewer remains available until you close it.

### What you can see

- **Living population:** one clickable tile per actual program, grouped into performance, novelty and drift pools. Color indicates a screened task candidate; a dot marks inherited blocks.
- **Verified discoveries:** first certified specimens, their birth times, and immediate parent/child comparisons. Certification means all 65,536 byte-input pairs passed.
- **Program microscope:** choose inputs, run the specimen, and step through register changes, memory, conditional skips and emitted answers. Inspect block definitions and the actual executed trace.
- **MAX → MIN case study:** replay the recorded one-instruction transition. Switch between original programs and clearly labeled human-reduced explanations. These examples never seed evolution.

`Pause view` pauses only the display. The engine writes completed snapshots every 10,000 births by default; the UI does not pretend to animate every birth.

### View your previous run without starting evolution

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run_lab.ps1 -View "out\cambrian01-seed-1-20260920-222213-261-95605b"
```

The viewer supports existing Cambrian-0.1 and new Cambrian-0.2 folders. It cannot add historical parent witnesses or blocks that an old run never recorded. Change the path for a different run.

### Linux / macOS

```bash
cmake -S lab -B build-lab -DCMAKE_BUILD_TYPE=Release
cmake --build build-lab --parallel
ctest --test-dir build-lab --output-on-failure
python3 scripts/lab.py --run out/new-lab-run --launch build-lab/driftvm_lab --births 10000000 --seed 1 --open
```

For a short first experiment use `--births 10000`, or `-Births 10000` in the Windows launcher.

## What the new experiment tests

A factoring mutation changes the **representation**, not the underlying machine. Blocks expand into the unchanged baseline VM; the fully expanded program remains limited to 96 primitive instructions. Conditional skips and primitive immediate values retain exactly the same semantics. There is no free computing reward for hiding operations inside a block.

Later mutations can reuse calls, modify shared bodies and compose blocks. This creates different paths through program space. The hypothesis is that this helps discover useful constructions; the implementation does not establish that hypothesis by itself.

Block depth, source compression, unfamiliar probe outputs and repeated references are **not** measures of intelligence or proof of useful abstraction. The resource suite still contains ten predefined tasks. This is not unrestricted language evolution, an open-ended task ecology, or LLM training.

Controlled runs within version 0.2:

```powershell
.\scripts\run_lab.ps1 -Births 1000000 -Seed 2
.\scripts\run_lab.ps1 -Births 1000000 -Seed 2 -NoModules
.\scripts\run_lab.ps1 -Births 1000000 -Seed 2 -NoDrift
.\scripts\run_lab.ps1 -Births 1000000 -Seed 2 -NoModules -NoDrift
```

Use multiple seeds and matched computational budgets. Each command needs its own terminal or a closed prior viewer. `NoDrift` removes protected random-replacement slots, not every form of stochastic drift. The new mutation rules change trajectories; old and new seed-1 results do not isolate one mechanism.

See **[the full Cambrian-0.2 protocol](docs/CAMBRIAN_02.md)** for persistence, ancestry replay, execution limits, validation, and interpretation.

## Baseline: Cambrian-0.1 remains available

The `driftvm` executable and `scripts/run_long.ps1` retain the corrected baseline:

- Current living task counts determine comparable ecological scores; rewards recover when carriers disappear.
- Separate performance, novelty and protected fitness-neutral drift populations.
- Candidate screening on 32 training and 128 screening inputs; exhaustive first-discovery and final-candidate verification over all 65,536 pairs.
- Founder genomes, exact admitted-child deltas, population inspection snapshots and full first-verified specimens.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run_long.ps1 -Births 10000000 -Seed 1
```

The root CMake build builds both engines and tests. The new launcher uses the standalone `lab` build. Old snapshots are inspection records, not resumable checkpoints.

For an old specimen:

```powershell
.\build\Release\driftvm.exe --inspect out\YOUR-RUN\discoveries\XOR.genome
python scripts\replay.py out\YOUR-RUN\lineage.tsv 12345 --out specimen.genome
```

Use an actual admitted ID. Old `lineage.tsv` and new `lineage-modules.tsv` have different replay readers; see the respective protocol. Keep all files in each result folder together. Millions of admitted births can still produce substantial lineage data.

[Baseline protocol](docs/CAMBRIAN_01.md) · [Original design and longer-term aspirations](docs/DESIGN.md)
