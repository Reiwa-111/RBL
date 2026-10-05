#!/usr/bin/env python3
"""RBL direct-ASM benchmark suite.

Three numbers are reported per workload:

  build - wall clock for RBL -> ASM -> object -> executable
  run   - mean wall clock of the whole process execution (what the original
          suite measured)
  net   - run minus the runtime of an empty program measured the same way

The ``net`` column exists because a small workload is dominated by process
start-up: on Windows every run also pays for the WSL bridge (or the PE loader),
which is why the absolute ``run`` numbers are only comparable on one machine.
"""
from __future__ import annotations
import statistics, sys, time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import rbltool

ROOT=Path(__file__).resolve().parents[1]
CASES=[('arith_100k','benchmarks/arith_100k.rbl'),('calls_20000','benchmarks/calls_20000.rbl'),('string_concat_1000','benchmarks/string_concat_1000.rbl')]
REPS=5

def measure(exe, settings, reps):
    """Return (mean, min, max, stdout) with one warm-up run discarded."""
    samples=[]; final=''
    for i in range(reps+1):
        t=time.perf_counter()
        rc,out,err=rbltool.run_built(exe, settings=settings)
        dt=time.perf_counter()-t
        if rc:
            raise RuntimeError(f'run failed (rc={rc}): {err.strip() or out.strip()}')
        final=out
        if i: samples.append(dt)
    samples=samples or [0.0]
    return statistics.mean(samples), min(samples), max(samples), final

def main():
    settings={}
    print('RBL direct ASM benchmark suite')
    print(f'Backend: {rbltool.active_backend(settings)}')
    print(f'Repetitions: {REPS}\n')

    # Startup baseline: an empty program, built and executed exactly like a case.
    base_dir = ROOT/'build'/'bench_baseline'
    base_dir.mkdir(parents=True, exist_ok=True)
    base_src = base_dir/'bench_baseline.rbl'
    base_src.write_text('func main() (\n    set x = 0\n)\n', encoding='utf-8')
    rc, log, base_exe = rbltool.build(base_src, settings)
    if rc:
        print('startup baseline build failed\n'+log)
        return 1
    baseline,_,_,_ = measure(base_exe, settings, REPS)
    print(f'startup baseline ({Path(base_exe).name}): {baseline*1000:8.2f} ms\n')

    for name,rel in CASES:
        src=ROOT/rel
        t0=time.perf_counter(); rc,build_out,info=rbltool.build(src,settings); build_s=time.perf_counter()-t0
        if rc:
            print(f'{name:24} BUILD FAIL\n{build_out}'); return 1
        exe=Path(info) if info else ROOT/'build'/src.stem/src.stem
        try:
            mean,lo,hi,out = measure(exe, settings, REPS)
        except RuntimeError as exc:
            print(f'{name:24} RUN FAIL\n{exc}'); return 1
        net = mean-baseline
        print(f'{name:24} build={build_s*1000:8.2f} ms  run={mean*1000:8.2f} ms  '
              f'min={lo*1000:8.2f} ms  max={hi*1000:8.2f} ms  net={net*1000:7.2f} ms  size={exe.stat().st_size:8d} B')
        print(f'  output: {out.strip()[:80]}')
    print('\nnet = run - baseline. A net value at or below zero means the workload is')
    print('below the start-up noise floor on this path (typical for the WSL bridge, where')
    print('every run pays for spawning wsl.exe). Compare net values on the same machine.')
    return 0
if __name__=='__main__': raise SystemExit(main())
