"""Run the 60-second kinematic regression without changing the motion model."""
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe, data = (Path(p).resolve() for p in sys.argv[1:3])
ref = (-3976219.5082, 3382372.5671, 3652512.9849)  # fixture header, not survey truth
inputs = [str(data / f) for f in ("07590920.05o", "30400920.05o", "07590920.05n", "30400920.05n")]
with tempfile.TemporaryDirectory() as tmp:
    wd = Path(tmp)
    for monitor in ("legacy", "apply"):
        cfg = wd / "rtk.conf"
        cfg.write_text(f"pos2-slipmon = {monitor}\npos1-dynamics = on\n", encoding="ascii")
        for direction in ([], ["-b"]):
            out = wd / "solution.pos"
            subprocess.run([str(exe), "-k", str(cfg), "-p", "2", "-f", "2", "-t", "-e",
                            "-r", "-3978242.4348", "3382841.1715", "3649902.7667",
                            "-ti", "60", "-te", "2005/04/02", "00:10:00", *direction,
                            "-o", str(out), *inputs], cwd=wd, capture_output=True, check=True,
                           env={**os.environ, "OPENBLAS_NUM_THREADS": "1"})
            rows = [line.split() for line in out.read_text().splitlines() if line.startswith("2005/")]
            assert sorted(row[1][:5] for row in rows) == [f"00:{i:02d}" for i in range(11)]
            distances = [math.dist(ref, [float(v) for v in row[2:5]]) for row in rows]
            assert all(math.isfinite(v) and v < 5.0 for v in distances), (monitor, direction, distances)
            assert any(row[5] == "1" for row in rows), "regression hidden by disabling fixing"
            assert all(all(float(x) > 0 and math.isfinite(float(x)) for x in r[7:10]) for r in rows)
