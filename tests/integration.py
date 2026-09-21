"""CLI, reproducibility, real ancestry replay and ablation regressions."""
import csv
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

exe = Path(sys.argv[1]).resolve()
spec = importlib.util.spec_from_file_location("replay", Path(__file__).parents[1] / "scripts" / "replay.py")
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)


def run(*args, success=True):
    result = subprocess.run([str(exe), *map(str, args)], text=True, capture_output=True, timeout=90)
    if (result.returncode == 0) != success:
        raise RuntimeError(f"Unexpected exit {result.returncode}: {result.stdout}\n{result.stderr}")
    return result


with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    for name in ("a", "b"):
        run("--out", root / name, "--births", 1200, "--population", 32, "--seed", 7,
            "--semantic-mutation-rate", .5, "--report-every", 600, "--all-events")
    for filename in ("lineage.tsv", "events.csv", "summary.txt", "discoveries.csv", "population-1200.tsv"):
        if (root / "a" / filename).read_bytes() != (root / "b" / filename).read_bytes():
            raise RuntimeError(f"Nondeterministic {filename}")
    with (root / "a" / "population-1200.tsv").open() as stream:
        final = list(csv.DictReader(stream, delimiter="\t"))
    for row in final:
        actual = replay.reconstruct(root / "a" / "lineage.tsv", int(row["id"]))
        if actual != row["genome"]:
            raise RuntimeError("Reconstruction differs from population snapshot")
    # Reconstruct an extinct accepted individual as well as all living individuals.
    with (root / "a" / "lineage.tsv").open() as stream:
        ancestry = list(csv.DictReader(stream, delimiter="\t"))
    extinct = next(row for row in ancestry[32:] if row["id"] not in {r["id"] for r in final})
    replay.reconstruct(root / "a" / "lineage.tsv", int(extinct["id"]))
    specimen = root / "specimen.genome"
    specimen.write_text("DRIFTVM_GENOME_1\n" + final[0]["genome"] + "\n")
    run("--inspect", specimen)
    preserved = (root / "a" / "lineage.tsv").read_bytes()
    run("--out", root / "a", "--births", 1, success=False)
    if preserved != (root / "a" / "lineage.tsv").read_bytes():
        raise RuntimeError("Existing experiment overwritten")
    run("--out", root / "control", "--births", 100, "--population", 32,
        "--drift-fraction", 0, "--novelty-fraction", 0, "--semantic-mutation-rate", 0, "--report-every", 0)
    control = (root / "control" / "lineage.tsv").read_text()
    if "\tS " in control or ";S " in control or "drift_" in control or "novelty_" in control:
        raise RuntimeError("Ablation leaked a disabled mechanism")
    run("--out", root / "zero", "--births", 0, "--report-every", 0, "--population", 8)
    for args in (("--semantic-mutation-rate", "nan"), ("--drift-fraction", "2"),
                 ("--population", "0"), ("--births", "-1"), ("--seed", "1junk"),
                 ("--drift-fraction", ".5", "--novelty-fraction", ".5"), ("--seed",)):
        run(*args, success=False)
    specimen.write_text("DRIFTVM_GENOME_1\nff:00\n")
    run("--inspect", specimen, success=False)
print("PASS integration: deterministic reruns, surviving/extinct genome replay, output protection, ablations, invalid inputs")
