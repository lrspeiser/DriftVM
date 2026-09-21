# DriftVM

C++20 hosts an evolutionary experiment. Each organism contains a small bytecode program and its own inherited mapping from 32 opcodes to four fixed micro-operations. Both the program and the opcode meanings can mutate. No LLM or native-code execution is involved.

## Current version: Cambrian-0.1

This corrects the Cambrian-0 baseline; it is not yet macro crystallization or an open-ended language generator.

- **Fair scoring:** parent and replacement comparisons use current living task counts, never stored historical scores. A task's reward recovers when its carriers disappear.
- **Protected populations:** by default 192 performance slots, 32 novelty slots and 32 fitness-neutral drift slots. Novelty is not added to computational performance. Ten percent of parent selections allow cross-pool transfer.
- **Verified discoveries:** 32 training inputs and 128 screening inputs identify candidates; a discovery is certified only after checking all 65,536 byte-input pairs. Every final candidate survivor is also checked exhaustively.
- **Real fossils:** full founder genomes, exact deltas for every admitted offspring, population snapshots, and complete first-verified specimens with disassembly and parent/mutation metadata.
- **Safe runs:** refuse existing output directories, validate arguments, and stop launchers on build/test failures.

The corrected experiment uses a new sampling algorithm and new selection rules. Seed 1 does not reproduce the old Cambrian-0 trajectory. Keep old result folders as baseline evidence, not as a control that isolates just one change.

## Windows: update, build, test, run

Inside your existing DriftVM folder in PowerShell:

```powershell
git pull --ff-only
if ($LASTEXITCODE -ne 0) { throw "Git update failed" }
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run_long.ps1 -Births 10000000 -Seed 1
```

The launcher builds Release, runs the regression tests and starts ten million births. It automatically creates a unique directory under `out/`. It never overwrites a previous experiment. Git, CMake and a C++20 compiler are required; Python 3 enables the additional integration/replay tests but is not required for the simulator or core tests.

For a shorter first run, use `-Births 100000`. Optional `-Out "out/my-new-experiment"` selects a **new** directory. Run the commands from your checkout folder.

## Linux / macOS

```bash
git pull --ff-only && bash scripts/run_long.sh 10000000 1
```

Manual build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/driftvm --births 100000 --seed 1 --out out/new-run
```

## Reading the output

`VERIFIED task=XOR ... inputs=65536` certifies that particular saved genome, not all its descendants. A first verified birth of zero means a random founder already had the capability.

`verified_tasks=5/10` counts task types ever certified. `best_candidate_quality` is a stable, unpenalized score for screened candidates, not a proof of correctness. `admitted_behaviors` counts distinct 32-probe output vectors ever admitted, not independent algorithms or complexity. `live_behaviors` counts their current diversity. Birth count and lineage depth are reported separately.

Summary task columns distinguish candidate evaluations, admitted candidate matches, final candidate carriers, and final **exhaustively verified** carriers. Repeated matches are not independent inventions. Certification is a separate measurement: screen-only false positives may still receive selection rewards, and the final check makes this limitation visible.

## Saved files

| File | Purpose |
|---|---|
| `config.txt` | Version, source revision, seeds, actual probes and configuration |
| `summary.txt` | Candidate counts versus verified outcomes |
| `progress.csv` | Time series of diversity, depth, acceptance and certified tasks |
| `events.csv` | Accepted births and every 10,000th rejected birth; `--all-events` saves all |
| `lineage.tsv` | All founders and exact mutation deltas for every admitted child |
| `population-N.tsv` | Full population at birth N; inspection snapshots, not resumable checkpoints |
| `discoveries.csv` | First certified specimen for each task |
| `discoveries/TASK.genome` and `.txt` | Executable genome and readable disassembly/provenance |
| `final-verification.csv` | Exhaustive verification of each final candidate survivor, including counterexamples |

Millions of admitted births can still produce hundreds of megabytes or more of lineage/event data. Population snapshots preserve everything alive at each report; ancestry deltas preserve extinct parents. Keep the output directory together. There is no automatic resume yet.

## Inspect or reconstruct a specimen

Windows:

```powershell
.\build\Release\driftvm.exe --inspect out\YOUR-RUN\discoveries\XOR.genome
python scripts\replay.py out\YOUR-RUN\lineage.tsv 12345 --out specimen.genome
.\build\Release\driftvm.exe --inspect specimen.genome
```

Replace `12345` with an actual admitted ID. The replay script verifies mutation preimages and uses memory proportional to the living population. A first-discovery child can have been rejected by selection; its full specimen and final mutation are still saved separately. No source programs from the old CSV-only run can be recovered without rerunning its original binary/configuration.

## Controlled comparisons

```powershell
.\scripts\run_long.ps1 -Births 1000000 -Seed 1
.\scripts\run_long.ps1 -Births 1000000 -Seed 1 -NoDrift
.\scripts\run_long.ps1 -Births 1000000 -Seed 1 -FixedLanguage
.\scripts\run_long.ps1 -Births 1000000 -Seed 1 -NoDrift -FixedLanguage
```

Use multiple seeds. `NoDrift` removes the protected random-replacement pool; it does **not** eliminate all stochastic drift (equal-score reproduction remains). `FixedLanguage` freezes each founder's dialect, not a single universal instruction set. Low-level options include `--novelty-fraction 0`, `--drift-fraction 0`, `--semantic-mutation-rate 0` and `--world-seed N`.

See [the corrected protocol](docs/CAMBRIAN_01.md) for limits and validation. The original [design notes](docs/DESIGN.md) describe the baseline and longer-term aspirations; their Cambrian-0 selection/logging details are superseded by this version.
