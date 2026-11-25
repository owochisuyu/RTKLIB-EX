"""Exercise the full post-processing path on the bundled GPS RINEX 2 fixture."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe, data = (Path(p).resolve() for p in sys.argv[1:3])
rover, base = data / "07590920.05o", data / "30400920.05o"
prn = 19
event = 10  # 00:05:00, on a 30 s grid


def inject(source, dest, spike):
    lines = source.read_text(encoding="ascii").splitlines(keepends=True)
    pos = next(i + 1 for i, s in enumerate(lines) if "END OF HEADER" in s)
    k = 0
    while pos < len(lines):
        h = lines[pos].rstrip()
        n = int(h[29:32])
        if h[28] == "4":  # post-header comments, including the final splice
            pos += n + 1
            continue
        assert n <= 12 and h[28] == "0"  # one row per sat in this specific fixture
        for j in range(n):
            if h[32 + 3*j:35 + 3*j] != f"G{prn:2d}" or k < event or (spike and k != event):
                continue
            row = lines[pos + 1 + j]
            for col in (0, 32):
                row = row[:col] + f"{float(row[col:col+14])+1.0:14.3f}" + row[col+14:]
            lines[pos + 1 + j] = row
        pos += n + 1
        k += 1
    dest.write_text("".join(lines), encoding="ascii")


def run(wd, name, mode, u=rover, b=base, extra=(), config=""):
    cfg, out = wd / (name + ".conf"), wd / (name + ".pos")
    cfg.write_text(f"pos2-slipmon = {mode}\npos2-arlockcnt = 0\npos1-dynamics = off\n"+config, encoding="ascii")
    args = [str(exe), "-k", str(cfg), "-p", "3", "-f", "2", "-t", "-e", "-x", "3", "-y", "2",
            "-r", "-3978242.4348", "3382841.1715", "3649902.7667",
            "-te", "2005/04/02", "00:10:00", *extra, "-o", str(out),
            str(u), str(b), str(data / "07590920.05n"), str(data / "30400920.05n")]
    proc = subprocess.run(args, cwd=wd, capture_output=True, env={**os.environ, "OPENBLAS_NUM_THREADS": "1"})
    assert proc.returncode == 0, proc.stderr[-400:]
    pos = [s for s in out.read_text().splitlines() if s.strip() and not s.startswith("%")]
    assert len(pos) >= 10, (name, pos)
    return pos, (wd / (name + ".pos.trace")).read_text(), (wd / (name + ".pos.stat")).read_text()


def actions(trace, station, time, action):
    return [s for s in trace.splitlines()
            if f"slipmon 2005/04/02 {time}.000 rcv={station} sat={prn} " in s and f"action={action} " in s]


def uses(trace, time, reset, skip):
    return [s for s in trace.splitlines()
            if f"slipmon-use 2005/04/02 {time}.000 sat={prn} " in s
            and f"reset={reset} skip={skip} ar_block=1" in s]


def status(stat, seconds):
    return [s.split(",") for s in stat.splitlines() if s.startswith("$SAT,")
            and s.split(",")[3] == f"G{prn:02d}" and float(s.split(",")[2]) % 86400 == seconds]


def replay(wd):
    step, spike, bstep = (wd / n for n in ("step.05o", "spike.05o", "base.05o"))
    inject(rover, step, False)
    inject(rover, spike, True)
    inject(base, bstep, False)
    for suffix, args in (("30", ()), ("60", ("-ti", "60"))):
        old = run(wd, "legacy"+suffix, "legacy", extra=args)
        shadow = run(wd, "shadow"+suffix, "shadow", extra=args)
        assert old[0] == shadow[0] and old[2] == shadow[2], "shadow changed solver output"
    clean = run(wd, "clean", "apply")
    lasting = run(wd, "lasting", "apply", u=step)
    one = run(wd, "one", "apply", u=spike)
    fixedbase = run(wd, "fixedbase", "apply", b=bstep)
    for result, station in ((lasting, 1), (one, 1), (fixedbase, 2)):
        assert len(actions(result[1], station, "00:05:00", "skip")) == 2
        assert len(uses(result[1], "00:05:00", 0, 1)) == 2
        sat = status(result[2], 300)
        assert len(sat) == 2 and all(s[9] == "0" and int(s[11]) < 2 for s in sat), sat
    for result, station in ((lasting, 1), (fixedbase, 2)):
        assert len(actions(result[1], station, "00:05:30", "reset")) == 2
        assert len(uses(result[1], "00:05:30", 1, 0)) == 2
        sat = status(result[2], 330)
        assert len(sat) == 2 and all(int(s[12]) & 1 and int(s[11]) < 2 for s in sat), sat
    assert not actions(one[1], 1, "00:05:30", "reset")
    assert not uses(one[1], "00:05:30", 1, 0)
    sat = status(one[2], 330)
    assert len(sat) == 2 and all(s[9] == "1" and not (int(s[12]) & 1) for s in sat), sat
    for t in ("00:05:00", "00:05:30"):
        assert not actions(clean[1], 1, t, "skip") and not actions(clean[1], 1, t, "reset")
    # With thinning, the confirmation is in an omitted raw epoch. Reversed
    # processing must consume the same boundary on the other side, only once.
    dec = run(wd, "dec", "apply", u=step, extra=("-ti", "60"))
    assert len(uses(dec[1], "00:06:00", 1, 0)) == 2
    back = run(wd, "back", "apply", u=step, extra=("-b",))
    assert len(uses(back[1], "00:05:00", 1, 1)) == 2
    iflc = run(wd, "iflc", "apply", u=step, config="pos1-ionoopt = dual-freq\n")
    isolated, reset = status(iflc[2], 300), status(iflc[2], 330)
    assert len(isolated) == len(reset) == 1
    assert isolated[0][9] == "0" and int(reset[0][12]) & 1 and int(reset[0][11]) < 2
    print(f"{len(lasting[0])} epochs; equal-slip, spike, fixed base, thinning, backward and shadow verified")


if __name__ == "__main__":
    if len(sys.argv) == 4:
        path = Path(sys.argv[3]).resolve()
        path.mkdir(exist_ok=True)
        replay(path)
    else:
        with tempfile.TemporaryDirectory() as tmp:
            replay(Path(tmp))
