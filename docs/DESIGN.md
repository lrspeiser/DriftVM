# DriftVM design notes

## Research question

Can an executable population maintain enough variation, neutral ancestry, and semantic mutation to discover computational behaviors that direct performance optimization systematically misses?

The long-term target is stronger: can a digital ecology continue creating reusable computational abstractions instead of converging on a fixed optimum?

## Why C++ plus custom bytecode?

Using native assembly would bind the experiment to an existing human-designed instruction set and make arbitrary mutation unsafe. Using C or another high-level language as the organism would make most random mutations syntactically invalid and compilation expensive.

Instead:

- C++ implements the deterministic VM and evolutionary scheduler.
- Organisms use a tiny total language: every byte sequence is executable.
- The language genome maps 32 organism-level opcodes onto short sequences of immutable micro-operations.
- Therefore syntax almost never kills an organism; semantics do.

This follows an important artificial-life / genetic-programming principle: mutations should usually produce bad programs, not unparsable programs.

## Cambrian-0

Cambrian-0 is the bootstrap experiment.

### Immutable substrate

The host provides simple micro-operations such as loads, moves, boolean/arithmetic operations, shifts, and `EMIT`.

These are analogous to physics: organisms cannot mutate them.

### Evolvable language

Each of 32 opcodes has four micro-op slots. The mapping is inherited. A rare mutation changes one slot, meaning descendants can use the same opcode number with a different semantic definition.

This is deliberately simpler than native machine code. The goal is to let us observe semantic lineage changes cleanly.

### Program genome

A program is a vector of opcode numbers. Program mutations can replace, insert, or delete opcodes.

### Environment

The world supplies hidden `(a,b)` inputs. An organism emits one byte for each probe. Its output vector is compared against resource functions such as XOR, ADD, SUB, AND, OR, MIN, MAX, and equality.

An organism is never told which function it should implement. It receives energy/score only when its behavior happens to match a resource over all probes.

Resource rewards fall as a behavior becomes prevalent, creating pressure toward rarer capabilities.

### Selection and drift

Selection is intentionally weak. A novel behavior receives a small survival bonus, and a small fixed fraction of mutants are allowed to survive despite failing to beat a comparison organism.

That last path is the explicit neutral-drift mechanism.

## The next experiment: Cambrian-1

Cambrian-1 should add the mechanism that makes the project genuinely about language evolution:

### Macro crystallization

If a lineage repeatedly executes the same useful sequence of opcodes, the system may crystallize that sequence into one new opcode. Descendants inherit the new primitive.

This creates a path:

`micro-ops -> repeated idiom -> macro -> instruction -> macro using prior macros`

The key measurement becomes **abstraction depth**: how many layers of evolved primitives separate a surviving instruction from the immutable substrate?

### Islands

Run many isolated populations with different mutation pressure and resource ecology. Rarely migrate organisms between them. This prevents one successful dialect from immediately taking over the entire experiment.

### Fossil archive

Never depend only on the living population. Preserve a sparse ancestry graph, semantic innovations, rare behavior signatures, and periodic population checkpoints. A currently useless ancestor may later prove essential to a breakthrough lineage.

## Cambrian-2: co-evolving problems

Introduce challenger organisms that generate input/output tasks or compact verifier programs.

A challenge earns resources when it separates the current solver population: trivial challenges and impossible challenges both earn little; challenges solved by only a minority earn more.

This creates an arms race without a human-authored static benchmark.

## What would count as a meaningful result?

Not merely a higher score.

Interesting evidence would include:

- innovation continuing rather than asymptoting quickly;
- stable incompatible language dialects;
- evolved opcodes surviving across long lineages;
- useful abstractions composed from older evolved abstractions;
- migration unlocking capabilities unavailable on either island alone;
- breakthrough descendants whose crucial ancestors survived only through neutral drift;
- resource functions or computational behaviors not explicitly present in the original hand-authored reward set.

The last point requires Cambrian-1/2 to stop using only a closed list of known functions and move toward behavioral novelty plus co-evolved verifiers.
