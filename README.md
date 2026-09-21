# DriftVM

**DriftVM is an artificial-life experiment in which programs and the meanings of their instructions can both evolve.**

The hypothesis is that useful novelty may require more than an optimizer repeatedly improving one answer. DriftVM creates a population of executable digital organisms, lets most mutations fail, preserves a small amount of neutral drift, and records the ancestry of whatever survives.

## What is it written in?

The **host simulator is C++20** for speed. The organisms do **not** run x86, ARM, or WebAssembly. They run a deliberately tiny custom bytecode.

Each organism contains:

1. a bytecode program made from 32 opcodes; and
2. a heritable **language genome** describing what each opcode means.

An opcode is currently implemented as four tiny micro-operations selected from a fixed substrate (`ADD`, `XOR`, `LOAD`, `EMIT`, and so on). Program mutations are common. Semantic mutations that change an opcode's meaning are rare.

That separation is intentional: C++ is merely the petri dish. The evolving bytecode is the organism.

## Build

Linux/macOS:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/driftvm --births 1000000 --population 256 --seed 1 --out out/run-1
```

Windows (Visual Studio / Developer PowerShell):

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\driftvm.exe --births 1000000 --population 256 --seed 1 --out out\run-1
```

For a quick smoke test:

```bash
./build/driftvm --births 10000 --report-every 1000
```

## What happens during a birth?

1. Select a living parent with mild tournament selection.
2. Copy its program and language genome.
3. Mutate 1-3 program locations.
4. Rarely mutate the semantics of one opcode.
5. Run the child on a fixed set of hidden input probes.
6. Observe the output behavior.
7. Reward exact discovery of computational resources such as XOR, ADD, AND, MAX, etc.
8. Give rare behaviors a small novelty bonus.
9. Preserve a tiny probability of otherwise-neutral survival (`drift`).
10. Record the birth, parent, behavior signature, score, and whether it survived.

Most births should fail. That is expected.

## Results

Each run writes:

- `events.csv` — one row per birth, including ancestry and whether the mutant survived.
- `summary.txt` — final run statistics and resource-discovery counts.

The first milestone is not to prove that DriftVM invents a new language. It is to establish a fast, deterministic evolutionary substrate that can execute millions of births and preserve enough ancestry to study rare successful lineages.

## Current experiment: Cambrian-0

This is intentionally minimal. It already supports:

- executable organisms;
- population-level selection;
- frequent program mutations;
- rare inherited instruction-semantic mutations;
- objective external evaluation;
- novelty preservation;
- explicit neutral drift;
- lineage logging;
- reproducible seeded runs.

It does **not yet** include the most important next-stage mechanisms:

- macros becoming new opcodes;
- multiple islands and migration;
- changing resource ecologies;
- co-evolving challenge generators;
- recombination;
- persistent fossil / MAP-Elites archives;
- open-ended behavior descriptors;
- LLM analysis of surviving lineages.

Those are deliberate follow-on experiments, not prerequisites for getting the evolutionary loop running.

See [`docs/DESIGN.md`](docs/DESIGN.md) for the architecture and research questions.
