# DriftSort 0.3: a useful, measurable objective

## Run on your Windows PC

From the existing checkout:

```powershell
git pull --ff-only
if ($LASTEXITCODE -ne 0) { throw "Git update failed" }
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run_sort.ps1 -Births 10000000 -Rounds 3 -Seed 1
```

This is ten million NEW attempted offspring **total**, split across three search/
native-measurement phases, not thirty million. A new unique output directory is
created. Earlier Cambrian programs and result folders are untouched. A local HTML
report opens in your browser; it refreshes while the experiment runs. Keep the
terminal open. Closing the browser does not stop the experiment. Ctrl+C terminates
the active child process; only COMPLETED search phases have resumable checkpoints.

Requires the CMake, C++20 and Python 3.9+ tools used for the Lab. Rust is measured
when `rustc` is on PATH. There are no package/API/GPU dependencies or automatic
software installations. Add `-RequireRust` to refuse starting without Rust. Without
Rust, the report says NOT MEASURED and cannot declare the combined goal met.

## The ordinary solutions and the target

The input is exactly eight `uint32_t`/`u32` values. The output must be the same
multiset in ascending order, including duplicates and the full unsigned range.

Normal C++:

```cpp
void sort8(std::array<uint32_t,8>& values) {
    std::sort(values.begin(), values.end());
}
```

Normal Rust:

```rust
fn sort8(values: &mut [u32; 8]) {
    values.sort_unstable();
}
```

Before evolution begins, the controller generates, compiles and benchmarks those
implementations. Frozen references also include ordinary insertion sort, a
28-exchange insertion network, and the **19-exchange Batcher odd-even merge
network**, with conditional-swap and min/max code generation. Rust gets the
standard-library and both 19-exchange variants. `baseline/generated.hpp` and
`baseline/generated.rs` contain the actual reference source used for the run.
All references are remeasured beside the candidates, not compared to old timings.

The predeclared local goal is a correct candidate at least **5% faster than every
measured reference on every one of five workloads**, with the adjusted paired-win
criterion below. A candidate beating only generic std::sort is not allowed to
claim it has beaten the specialized reference. Passing requires Rust to have been
measured before claiming success against both compiler/library stacks.

This is a task-specific program/language search, **not a ranking of C++ and Rust
as languages, not unrestricted language invention, and not an asymptotic sorting
breakthrough**. The result is exported to ordinary C++ and Rust. Neither the
existence of blocks nor a shorter source listing is treated as a speedup.

## What actually evolves

A genome has a top-level program and up to 16 inherited, mutable, nestable
instructions (blocks). The fixed safe substrate contains:

- `CX(a,b)`: put min on register a and max on register b.
- `MIN(a,b)` / `MAX(a,b)`: update just register a.
- `COPY(a,b)`: copy b into a.

There are eight input/output registers. Mutation changes wiring, operation type,
sequence order and length, duplicate calls, shared instruction bodies and their
composition. Factoring packages an existing sequence, never a hand-supplied useful
new body. Every expanded program is bounded by 64 primitives and nesting depth 8.
Blocks are inherited vocabulary, not new physical CPU instructions. All block
calls are expanded for verification and native code generation.

32 of 256 founders contain the ordinary 28-exchange insertion network; the rest
are random. **This is a declared warm-start improvement experiment.** It is not a
claim of rediscovering sorting from nothing. The stronger 19-exchange comparator
reference is not seeded into evolution.

192 performance slots favor fewer wrong output bits, then a static operation-cost
proxy, then a dependency-depth proxy. 32 novelty slots preserve unfamiliar binary
output signatures in a bounded recent-history table. 32 drift slots accept valid
mutants without performance selection. Some cross-pool reproduction is allowed.
`CX` costs two in the proxy, other primitives one. This is not a CPU cost model.

At each phase boundary a small proxy-ranked archive is compiled into both
min/max and conditional forms in C++ and, when available, Rust. The fastest
measured candidate representations become additional breeding parents in the
next phase; the previous measured champion is included in later tournaments.
Most births never incur compilation or native timing. Thus this is a hybrid:
cheap exact verification/proxy search plus periodic measured-performance feedback.

## Why 256 test inputs can certify this restricted language

All 256 binary input vectors are evaluated in parallel with four 64-bit words
per register. Every output coordinate is compared to the correct sorted binary
vector, not just to its neighbor. This matters: a COPY-only program can appear
sorted while silently destroying the multiset.

For this straight-line **min/max/copy-only** language, each operation commutes
with every monotone threshold map. Therefore the complete circuit commutes with
thresholding. If its output differed from the correct sorted result at any
coordinate for arbitrary unsigned inputs, a threshold between those two values
would produce a differing coordinate on a binary input, contradicting exhaustive
binary agreement. This extends the familiar zero-one argument to the single-output
min/max/copy primitives used here. There are no arithmetic or input-dependent
address operations that would invalidate that argument.

C++ scalar, C++ bit-parallel, and independent Python interpreters are cross-checked.
The actual compiled native variants are independently audited on all binary
vectors and thousands of uint32 arrays before timing. Every timed batch is also
checked against the expected result, outside the timer. These checks do not prove
an arbitrary compiler correct; the mathematical certificate applies to the DSL
semantics and its stated lowering rules. The UI example alone is not a proof.

## Native measurement protocol and its limits

Five equally weighted workloads: random uint32, already sorted, reverse sorted,
four-value duplicates, and sorted input with one random pair swapped.

Each timing sample receives a fresh copy of the same unsorted source batch for
its workload. Copies and validation are outside timing. Warmup precedes samples;
implementation order is randomized each round. C++ and Rust share one C++ timing
harness and one function-call boundary per batch, not one costly cross-language
call per eight-element sort. Default batch size: 32,768 arrays. These are
**batched-throughput measurements**, not single-sort latency. CPU, compiler
versions, generated sources, flags/build commands and raw timings are recorded.

Tuning uses 15 trials/workload. A single finalist is chosen by geometric mean of
its per-workload median times. This choice is frozen in selection.json before
confirmation. Confirmation uses a distinct input seed and 31 new trials/workload;
there is no reselection from confirmation results. A baseline/case passes when
the median paired baseline/candidate ratio exceeds 1.05 and the one-sided binomial
sign-tail for ratios exceeding 1.05 is below 0.05 divided by the number of
baseline/workload checks. Every check must pass for the relevant goal.

That is a conservative **local acceptance rule**, not proof of universal superiority.
Scheduling, power management, correlated timing noise, compiler layouts and input
model choices remain limitations. Repeat a claimed win in a fresh machine session
and compare other compilers/CPUs before making broader claims. This baseline set
is useful but is not an exhaustive state-of-the-art survey. The controller never
decides that an unfamiliar program is inherently innovative.

## Results and continuation

`report.html` is an interactive, local-only scoreboard with native timings and a
step-through sorter. `manifest.json` records options, source hashes and hardware;
`selection.json` records the frozen finalist; `confirmation.csv` stores raw held-out
measurements; `verdict.json` stores every pass/fail check. `winner.cpp`, `winner.rs`
and `winner.genome` export the selected routine even when it does not beat the
reference. Do not deploy an underperforming winner just because it is called winner.

Each `search-N/checkpoint.txt` preserves the population, RNG, archive, measured
breeding parents and recent novelty state. Completed checkpoints can be continued
in a NEW output directory with the same seed and controls:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run_sort.ps1 -Births 10000000 -Rounds 3 -Seed 1 -Resume "out\YOUR-RUN\search-3\checkpoint.txt"
```

The checkpoint alone preserves search state. Native-parent feedback alongside it
is picked up automatically. Repeating native timings introduces measurement noise,
so a complete adaptive experiment is not bit-for-bit reproducible from seed alone.
Core search restart without new feedback is tested to be identical.

Use `-NoModules` and `-NoDrift` for ablations, across multiple seeds and declared
budgets. NoDrift reallocates the protected pool to performance, not all randomness.
The sorting track saves full archived specimens but not every extinct ancestor.
Old Cambrian outputs cannot be resumed into this different substrate.

## Sources

- Mankowitz et al., *Faster sorting algorithms discovered using deep reinforcement
  learning*, Nature 618 (2023), https://doi.org/10.1038/s41586-023-06004-9 .
  Motivation for separating inexpensive search proxies from native timing.
- H. W. Lang, *The 0-1-principle*,
  https://hwlang.de/algorithmen/sortieren/networks/nulleinsen.htm .
  Threshold-commutation proof used above; the scope extension is explained explicitly.
- Bert Dobbelaere, sorting-network reference catalogue,
  https://bertdobbelaere.github.io/sorting_networks.html .
  Documents the eight-input 19-comparator reference class. Our Batcher topology is
  generated independently by the odd-even merge recursion, not scraped source code.
- Rust slice API, https://doc.rust-lang.org/std/primitive.slice.html#method.sort_unstable .
- Rust linkage reference, https://doc.rust-lang.org/reference/linkage.html .
  Native `cdylib`/C ABI is used for the shared timing harness.
