# Cambrian-0.1: corrected selection and auditable discoveries

## Preserve the baseline

The user-reported Windows Cambrian-0 run (seed 1, 10,000,000 births, 256 organisms) reported 78,925 admitted probe signatures and nine nonzero task counters. Those counters were cumulative admitted matches, not verified discoveries. The old reward denominator used lifetime matches, while incumbent scores were never refreshed. The event CSV lacked genomes and exact edits. Keep those original artifacts intact; this new version is a different experiment.

## Selection contract

The immutable VM and its original implicit final-register output are retained. Programs are forward-only and bounded by 96 opcodes, each with four micro-operations. There are no backward jumps, host filesystem/network operations, or native execution.

The default population is partitioned into 192 performance, 32 novelty and 32 drift slots. A uniformly chosen replacement slot determines the offspring's destination pool. Ordinarily the parent is drawn inside that pool, with a two-parent tournament for performance/novelty and uniform sampling for drift. Ten percent of births instead draw a parent uniformly from the full population. This preserves protected reproductive opportunities while allowing transfer.

For performance, both tournament candidates and both replacement candidates are scored on the same pre-replacement living occupancy:

`score = sum(base_task_reward / (1 + current_task_carriers))`

Counts are decremented on death and incremented on admission. There is no stored fitness. Raw candidate quality uses the fixed task rewards without crowding. Novelty has its own pool and uses `1 / (1 + historical_admissions_of_this_probe_output_vector)`. Exact output vectors are map keys, so hash collisions do not collapse categories. Drift replacements are fitness-independent. Equal-score replacements in the other pools are accepted with probability one half; neutral reproduction is possible without the protected drift pool.

This is not a claim that 75/12.5/12.5 is optimal, and it is not pure objective-free evolution. These are declared experimental settings to compare, not measured natural constants.

## Verification contract

The two fixed banks contain 32 training and 128 screening inputs. World seed is independent of evolutionary seed and defaults to 20260920. Screening is selection feedback, not an untouched scientific test set.

A candidate for a task not yet certified is checked over every one of the 65,536 possible `(a,b)` byte inputs. Failure stops at the first counterexample. A bounded cache avoids repeatedly rechecking the exact same failed genome. Only fully passing genomes appear in the first-discovery archive. After one proof for a task, new candidates for that task are not all re-proved during search. Every final candidate survivor is exhaustively checked regardless of that archive.

Consequently, selection still operates on sampled candidate behavior; the summary explicitly reveals false-positive final carriers. A correct ancestor does not certify its mutated descendants. Proof here covers only the finite input domain and the declared VM, not arbitrary inputs or a general algorithmic novelty claim. The verifier cannot produce faster native execution or scientific originality certificates.

The hand-constructed controls live only in tests. In particular, the four-opcode ROTATE_XOR control is verified on all inputs but never seeded into populations or injected via migration.

## Persistence contract

All founders are recorded in full; every admitted child records exact ordered program and semantic edits, old values, parent, replacement, destination and depth. A replaced parent is removed only after its child's delta is reconstructed. Replay can reconstruct extinct individuals with O(population) memory. Population snapshots and first discoveries additionally contain complete genomes. The final mutation of a first-discovery child is saved even if that child is rejected by selection.

Failed births usually only affect statistics; events retains every 10,000th rejected birth, or all births with `--all-events`. First-discovery snapshots are never discarded. Files are flushed on reporting intervals; abrupt power loss can lose a buffered suffix. Snapshots are for inspection, not RNG-state resume. A new run refuses an existing directory, and launchers create unique directories.

## Reproducibility and comparisons

The PRNG uses mt19937_64 with explicit rejection sampling and a fixed double conversion, avoiding implementation-dependent uniform distributions. Same executable/configuration yields identical lineage and numerical event records; elapsed wall time is intentionally not deterministic. Metadata includes revision, seeds, configuration, and probe banks. Building from modified sources under an existing Git revision still requires retaining that working-tree patch separately.

Compare full, no protected drift pool, frozen founder dialects, and both changes, across multiple evolutionary and world seeds. Also compare a performance-only condition by setting both pool fractions to zero. Report births, VM work where available, verifier-input cost, and wall time; equal births alone do not guarantee equal compute. This release records exhaustive-verifier input counts and elapsed time but does not yet instrument every VM micro-operation. Do not describe NoDrift as removal of all population-genetic drift.

Primary endpoints: probability and birth of first certified task discovery, retained verified diversity, lineage depth, semantic changes, and transfer histories. Output-signature growth alone is not useful innovation. To establish whether a historical mutation caused a later discovery requires matched interventions, not merely pointing to that mutation in a successful ancestry.

## Validation performed during implementation

- GCC Release build, no warnings after cleanup.
- Ten positive-control genomes checked exhaustively, including ROTATE_XOR; negative verification and independent parity oracle.
- Mutation bounds, frozen semantics, serialization, live-occupancy reward recovery, implicit output semantics.
- Deterministic reruns; all final genomes reconstructed from deltas plus an extinct admitted specimen.
- Existing output protection, disabled-pool tests, zero births, zero report interval, malformed numbers and genomes.
- Local 100,000-birth seed-1 run: five tasks certified (AND was present in a founder), with false-positive sampled candidates detected by final verification. This is validation of the measurement pipeline, not evidence that drift beats a control.

The CI workflow builds and tests Linux and Windows, including their actual launcher scripts. Its live status must be checked separately; declaring jobs does not mean they have passed.

## Still not implemented

Macro crystallization, instructions composed of evolved instructions, recombination, co-evolving tasks, a general-purpose evolving language, automated LLM interpretation, and resumable checkpoints remain future work. This release fixes the experimental foundation rather than claiming to deliver those mechanisms.
