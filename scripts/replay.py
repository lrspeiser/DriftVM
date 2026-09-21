#!/usr/bin/env python3
"""Reconstruct an admitted organism from exact lineage deltas, using O(population) memory."""
import argparse
import csv
from pathlib import Path


def reconstruct(lineage: Path, target: int) -> str:
    live: dict[int, tuple[list[int], list[int]]] = {}
    with lineage.open(encoding="utf-8", newline="") as stream:
        for row in csv.DictReader(stream, delimiter="\t"):
            oid, parent = int(row["id"]), int(row["parent"])
            delta = row["mutation"]
            if parent == 0:
                if not delta.startswith("F "):
                    raise ValueError("Founder has no full genome")
                p, lang = delta[2:].split(":")
                program, language = list(bytes.fromhex(p)), list(bytes.fromhex(lang))
            else:
                if parent not in live:
                    raise ValueError(f"Missing living parent {parent}; incomplete lineage")
                program, language = (v.copy() for v in live[parent])
                for operation in delta.split(";"):
                    if not operation:
                        continue
                    kind, *values = operation.split()
                    values = list(map(int, values))
                    if kind == "I":
                        pos, value = values
                        if not 0 <= pos <= len(program):
                            raise ValueError("Invalid insertion position")
                        program.insert(pos, value)
                    elif kind == "D":
                        pos, old = values
                        if program[pos] != old:
                            raise ValueError("Deletion preimage mismatch")
                        del program[pos]
                    elif kind == "P":
                        pos, old, value = values
                        if program[pos] != old:
                            raise ValueError("Replacement preimage mismatch")
                        program[pos] = value
                    elif kind == "S":
                        code, slot, old, value = values
                        pos = code * 4 + slot
                        if language[pos] != old:
                            raise ValueError("Semantic preimage mismatch")
                        language[pos] = value
                    else:
                        raise ValueError(f"Unknown mutation: {kind}")
            if not 4 <= len(program) <= 96 or len(language) != 128:
                raise ValueError("Invalid reconstructed genome length")
            if any(x >= 32 for x in program) or any(x >= 21 for x in language):
                raise ValueError("Invalid reconstructed instruction")
            if oid == target:
                return bytes(program).hex() + ":" + bytes(language).hex()
            replaced = int(row["replaced"])
            if replaced:
                if replaced not in live:
                    raise ValueError("Missing replaced organism")
                del live[replaced]
            live[oid] = (program, language)
    raise ValueError(f"ID {target} not found among founders/admitted offspring")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("lineage", type=Path)
    parser.add_argument("id", type=int)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    data = reconstruct(args.lineage, args.id)
    # Refuse to overwrite an earlier specimen.
    with args.out.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write("DRIFTVM_GENOME_1\n" + data + "\n")
    print(f"Reconstructed organism {args.id}: {args.out}")


if __name__ == "__main__":
    main()
