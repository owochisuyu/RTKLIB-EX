"""Check the selected engine, not just whether the output says FIX."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

exe, data = (Path(x).resolve() for x in sys.argv[1:3])
inputs = [str(data / f) for f in ("07590920.05o", "30400920.05o", "07590920.05n", "30400920.05n")]
decision = re.compile(r"AR decision time=(\S+ \S+) search=(\w+) n=(\d+)/(\d+) pairs=(\d+)"
                      r" test=(\w+) s1=\S+ s2=\S+ accepted=(\d+) strategy=(\w+)")


def solve(wd, name, options, nf=2, error=False, minutes=12):
    cfg, out = wd / (name + ".conf"), wd / (name + ".pos")
    cfg.write_text("pos1-dynamics = off\npos2-arthres1 = 100\n" + options + "\n", encoding="ascii")
    run = subprocess.run([str(exe), "-k", str(cfg), "-p", "3", "-f", str(nf), "-t", "-e",
                          "-te", "2005/04/02", f"00:{minutes:02d}:00", "-x", "2",
                          "-r", "-3978242.4348", "3382841.1715", "3649902.7667", "-o", str(out),
                          *inputs], cwd=wd, capture_output=True, env={**os.environ, "OPENBLAS_NUM_THREADS": "1"})
    if error:
        assert run.returncode and b"pos2-arstrategy" in run.stderr and not out.exists(), name
        return
    assert run.returncode == 0, (name, run.stderr[-500:])
    pos = [s for s in out.read_text().splitlines() if s.startswith("2005/")]
    assert len(pos) == 1 + 2*minutes, (name, len(pos))
    trace = Path(str(out) + ".trace").read_text()
    decisions = decision.findall(trace)
    assert len(decisions) == trace.count("AR decision time="), name
    assert len({r[0] for r in decisions}) == len(decisions), (name, "multiple attempts in an epoch")
    return pos, decisions, trace


def check(wd):
    runs = {}
    for mode in ("full", "wlnl", "par"):
        runs[mode] = solve(wd, mode, "pos2-arstrategy = " + mode)
        _, rows, _ = runs[mode]
        assert rows and all(r[7] == mode and r[5] == "ratio" for r in rows)
        assert all(r[1] == ("wlnl" if mode == "wlnl" else "direct") for r in rows)
        assert all((int(r[4]) > 0) == (mode == "wlnl") for r in rows)
        if mode != "par":
            assert all(r[2] == r[3] for r in rows)
        rejected, attempts, _ = solve(wd, mode+"-reject", "pos2-arstrategy = "+mode+"\npos2-arthres = 1e15")
        assert attempts and all(r[6] == "0" and r[7] == mode for r in attempts)
        assert all(line.split()[5] != "1" for line in rejected)
    for i, (options, mode) in enumerate((
            ("", "wlnl"), ("pos2-par = off", "wlnl"), ("pos2-wlnl = off", "full"),
            ("pos2-par = on", "par"), ("pos2-wlnl = on\npos2-par = off", "wlnl"),
            ("pos2-wlnl = off\npos2-par = off", "full"),
            ("pos2-arstrategy = par\npos2-wlnl = on\npos2-par = on", "par"),
            ("pos2-wlnl = on\npos2-par = on\npos2-arstrategy = par", "par"))):
        got = solve(wd, "compat"+str(i), options)
        assert got[:2] == runs[mode][:2], (i, mode)
    for i, options in enumerate(("pos2-par = on\npos2-wlnl = on", "pos2-arstrategy = p",
                                 "pos2-arstrategy = full\npos2-arstrategy = par")):
        solve(wd, "invalid"+str(i), options, error=True)
    if len(sys.argv) > 3:
        rcv = subprocess.run([str(Path(sys.argv[3]).resolve()), "-o", str(wd/"invalid0.conf")],
                             cwd=wd, stdin=subprocess.DEVNULL, capture_output=True, timeout=5)
        assert rcv.returncode and b"choose one pos2-arstrategy" in rcv.stderr
    # Keep the production PAR success floor. Give L2 less weight than L1
    # in this test only, so the two-frequency fixture exercises a true subset.
    _, long_par, _ = solve(wd, "weak-l2-par", "pos2-arstrategy = par\nstats-eratio2 = 6000", minutes=59)
    subsets = sorted({r[2]+"/"+r[3] for r in long_par if int(r[2]) < int(r[3])})
    assert subsets and all(r[1] == "direct" and r[7] == "par" for r in long_par)
    print("PAR with downweighted L2: " + ", ".join(subsets))
    single, attempts, trace = solve(wd, "single-wlnl", "pos2-arstrategy = wlnl", nf=1)
    assert not attempts and "AR no matched lanes" in trace
    assert all(line.split()[5] != "1" for line in single)
    for m in runs:
        dims = sorted({r[2]+"/"+r[3] for r in runs[m][1]})
        print(f"{m}: {len(runs[m][1])} decisions; selected/available {', '.join(dims)}")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        check(Path(tmp))
