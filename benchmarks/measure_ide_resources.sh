#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
EXECUTABLE="${RBL_STUDIO_EXE:-$ROOT/target/release/rbl-studio}"
WARMUP_SECONDS="${RBL_STUDIO_WARMUP:-5}"
SAMPLE_SECONDS="${RBL_STUDIO_SAMPLES:-20}"
SAMPLE_INTERVAL_MS="${RBL_STUDIO_SAMPLE_INTERVAL_MS:-250}"
MEMORY_LIMIT_MIB="${RBL_STUDIO_MEMORY_LIMIT_MIB:-512}"
SCENARIO="${RBL_STUDIO_SCENARIO:-Idle}"
AUTOMATION_TIMEOUT_SECONDS="${RBL_STUDIO_AUTOMATION_TIMEOUT_SECONDS:-20}"
export RBL_STUDIO_MEMORY_LIMIT_MIB="$MEMORY_LIMIT_MIB"
fixture_dir=""
automation_pid=""
generated_fixtures=0
documents=("$@")

[[ "$WARMUP_SECONDS" =~ ^[0-9]+$ ]] && (( WARMUP_SECONDS <= 120 )) || {
    printf 'Warm-up must be an integer between 0 and 120 seconds.\n' >&2
    exit 2
}
[[ "$SAMPLE_SECONDS" =~ ^[0-9]+$ ]] && (( SAMPLE_SECONDS >= 1 && SAMPLE_SECONDS <= 300 )) || {
    printf 'Sample duration must be an integer between 1 and 300 seconds.\n' >&2
    exit 2
}
[[ "$SAMPLE_INTERVAL_MS" =~ ^[0-9]+$ ]] && (( SAMPLE_INTERVAL_MS >= 50 && SAMPLE_INTERVAL_MS <= 5000 )) || {
    printf 'Sample interval must be an integer between 50 and 5000 milliseconds.\n' >&2
    exit 2
}
[[ "$MEMORY_LIMIT_MIB" =~ ^[0-9]+$ ]] && (( MEMORY_LIMIT_MIB >= 1 && MEMORY_LIMIT_MIB <= 8192 )) || {
    printf 'Memory limit must be an integer between 1 and 8192 MiB.\n' >&2
    exit 2
}
[[ "$AUTOMATION_TIMEOUT_SECONDS" =~ ^[0-9]+$ ]] && (( AUTOMATION_TIMEOUT_SECONDS >= 5 && AUTOMATION_TIMEOUT_SECONDS <= 300 )) || {
    printf 'RBL_STUDIO_AUTOMATION_TIMEOUT_SECONDS must be an integer from 5 to 300.\n' >&2
    exit 2
}
case "$SCENARIO" in
    Idle|SingleDocument|32Tabs|EditUndoRedo|FindReplaceUndoRedo|CompilerTask|TestSuite) ;;
    *) printf 'Unknown IDE resource scenario: %s\n' "$SCENARIO" >&2; exit 2 ;;
esac

if [[ ! -x "$EXECUTABLE" ]]; then
    printf 'Executable not found or not executable: %s\n' "$EXECUTABLE" >&2
    exit 2
fi

if [[ "$SCENARIO" != "Idle" && ${#documents[@]} -eq 0 ]]; then
    generated_fixtures=1
    fixture_dir="$(mktemp -d "${TMPDIR:-/tmp}/rbl-studio-resource.XXXXXX")"
    python3 - "$SCENARIO" "$fixture_dir" "$ROOT" >"$fixture_dir/documents.txt" <<'PY'
import os
import sys

scenario, directory, root = sys.argv[1:]

def write_repeated(path, size, pattern):
    pattern = pattern.encode("ascii")
    chunk = (pattern * ((65536 + len(pattern) - 1) // len(pattern)))[:65536]
    with open(path, "wb") as stream:
        remaining = size
        while remaining:
            block = chunk[: min(remaining, len(chunk))]
            stream.write(block)
            remaining -= len(block)

paths = []
if scenario == "SingleDocument":
    path = os.path.join(directory, "large-16mib.txt")
    write_repeated(path, 16 * 1024 * 1024, "RBL_RESOURCE_SAMPLE ")
    paths.append(path)
elif scenario == "32Tabs":
    for index in range(32):
        path = os.path.join(directory, f"tab-{index:02d}.txt")
        write_repeated(path, 1024 * 1024, f"TAB{index:02d}_RESOURCE_SAMPLE ")
        paths.append(path)
elif scenario == "EditUndoRedo":
    for index in range(4):
        path = os.path.join(directory, f"edit-{index:02d}.txt")
        write_repeated(path, 8 * 1024 * 1024, f"EDIT{index:02d}_RESOURCE_SAMPLE ")
        paths.append(path)
elif scenario == "FindReplaceUndoRedo":
    path = os.path.join(directory, "find-replace-16mib.txt")
    write_repeated(path, 16 * 1024 * 1024, "needle ")
    paths.append(path)
elif scenario == "CompilerTask":
    paths.append(os.path.join(root, "tests", "cases", "gc_pressure.rbl"))
elif scenario == "TestSuite":
    pass

for path in paths:
    print(path)
PY
    mapfile -t documents < "$fixture_dir/documents.txt"
fi

if [[ "$SCENARIO" =~ ^(EditUndoRedo|FindReplaceUndoRedo|CompilerTask|TestSuite)$ && -z "$fixture_dir" ]]; then
    fixture_dir="$(mktemp -d "${TMPDIR:-/tmp}/rbl-studio-resource.XXXXXX")"
fi
if [[ "$SCENARIO" =~ ^(EditUndoRedo|FindReplaceUndoRedo|CompilerTask|TestSuite)$ ]]; then
    printf '%s\n' "${documents[@]}" > "$fixture_dir/documents.txt"
fi

command -v setsid >/dev/null 2>&1 || {
    printf 'setsid is required for accurate IDE process-group sampling.\n' >&2
    exit 2
}
setsid "$EXECUTABLE" "${documents[@]}" &
pid=$!
cleanup() {
    if [[ -n "$automation_pid" ]]; then
        kill "$automation_pid" 2>/dev/null || true
    fi
    kill -TERM -- "-$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
    sleep 0.2
    kill -KILL -- "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    if [[ -n "$fixture_dir" ]]; then
        rm -rf -- "$fixture_dir"
    fi
}
trap cleanup EXIT INT TERM

sleep "$WARMUP_SECONDS"
kill -0 "$pid" 2>/dev/null || {
    printf 'RBL Studio exited during the warm-up period.\n' >&2
    exit 3
}

if [[ "$SCENARIO" =~ ^(EditUndoRedo|FindReplaceUndoRedo|CompilerTask|TestSuite)$ ]]; then
    command -v timeout >/dev/null 2>&1 || { printf 'GNU timeout is required for bounded UI automation.\n' >&2; exit 2; }
    timeout --signal=TERM --kill-after=2 "${AUTOMATION_TIMEOUT_SECONDS}s" \
        python3 "$SCRIPT_DIR/ide_x11_automation.py" "$SCENARIO" --pid "$pid" \
        --documents-file "$fixture_dir/documents.txt" >"$fixture_dir/automation.log" 2>&1 &
    automation_pid=$!
fi

python3 - "$pid" "$SAMPLE_SECONDS" "$SAMPLE_INTERVAL_MS" "$EXECUTABLE" "$ROOT" "$SCENARIO" <<'PY'
import hashlib
import os
import platform
import re
import sys
import time

pid = int(sys.argv[1])
sample_seconds = int(sys.argv[2])
sample_interval_ms = int(sys.argv[3])
executable = os.path.realpath(sys.argv[4])
root = os.path.realpath(sys.argv[5])
scenario = sys.argv[6]
with open(executable, "rb") as binary:
    executable_hash = hashlib.sha256(binary.read()).hexdigest()
source_hashes = []
for relative in ("src/main.rs", "src/app.rs", "src/editor.rs", "src/process.rs", "src/settings.rs", "Cargo.toml", "Cargo.lock"):
    with open(os.path.join(root, relative), "rb") as source:
        source_hashes.append(hashlib.sha256(source.read()).hexdigest())
source_fingerprint = hashlib.sha256("".join(source_hashes).encode()).hexdigest()
with open(os.path.join(root, "Cargo.toml"), encoding="utf-8") as cargo:
    cargo_version_match = re.search(r'^version\s*=\s*"([^"]+)"', cargo.read(), re.MULTILINE)
cargo_version = cargo_version_match.group(1) if cargo_version_match else "unknown"
ticks_per_second = os.sysconf(os.sysconf_names["SC_CLK_TCK"])
logical_processors = os.cpu_count() or 1
samples = []
sample_start = time.monotonic()
sample_deadline = sample_start + sample_seconds


def read_process(pid):
    try:
        stat = open(f"/proc/{pid}/stat", encoding="ascii").read()
        rest = stat[stat.rfind(")") + 2 :].split()
        state = rest[0]
        parent_pid = int(rest[1])
        process_group = int(rest[2])
        cpu_ticks = int(rest[11]) + int(rest[12])  # utime + stime
        start_ticks = int(rest[19])
        status = open(f"/proc/{pid}/status", encoding="ascii").read()
        fields = {
            line.split(":", 1)[0]: line.split(":", 1)[1].strip()
            for line in status.splitlines()
            if ":" in line
        }
        rss = int(fields.get("VmRSS", "0 kB").split()[0])
        data = int(fields.get("VmData", "0 kB").split()[0])
        pss = None
        private = None
        try:
            rollup = open(f"/proc/{pid}/smaps_rollup", encoding="ascii").read()
            pss = 0
            private = 0
            for line in rollup.splitlines():
                if line.startswith("Pss:"):
                    pss = int(line.split()[1])
                elif line.startswith("Private_Clean:") or line.startswith("Private_Dirty:"):
                    private += int(line.split()[1])
        except (FileNotFoundError, ProcessLookupError, PermissionError, ValueError, IndexError):
            pass
        return state, parent_pid, process_group, rss, data, cpu_ticks, start_ticks, pss, private
    except (FileNotFoundError, ProcessLookupError, PermissionError, ValueError, IndexError):
        return None


def process_tree(root_pid):
    processes = {}
    children = {}
    try:
        entries = os.scandir("/proc")
    except OSError:
        return None
    with entries:
        for entry in entries:
            if not entry.name.isdigit():
                continue
            pid_value = int(entry.name)
            record = read_process(pid_value)
            if record is None:
                continue
            processes[pid_value] = record
            children.setdefault(record[1], []).append(pid_value)
    if root_pid not in processes:
        return None
    root_group = processes[root_pid][2]
    # The IDE owns a dedicated process group. Include members even after their
    # immediate parent exits and they are reparented by the Linux kernel.
    members = {
        pid_value
        for pid_value, record in processes.items()
        if record[2] == root_group
    }
    pending = list(members)
    while pending:
        parent = pending.pop()
        for child in children.get(parent, ()):
            if child not in members:
                members.add(child)
                pending.append(child)
    root = processes[root_pid]
    descendants = [processes[pid_value] for pid_value in members if pid_value != root_pid]
    pss_values = [processes[pid_value][7] for pid_value in members]
    private_values = [processes[pid_value][8] for pid_value in members]
    child_pss_values = [item[7] for item in descendants]
    child_private_values = [item[8] for item in descendants]
    return {
        "root_state": root[0],
        "root_rss": root[3],
        "root_data": root[4],
        "root_cpu": root[5],
        "root_pss": root[7],
        "root_private": root[8],
        "tree_rss": sum(processes[pid_value][3] for pid_value in members),
        "tree_data": sum(processes[pid_value][4] for pid_value in members),
        "tree_pss": sum(pss_values) if all(value is not None for value in pss_values) else None,
        "tree_private": sum(private_values) if all(value is not None for value in private_values) else None,
        "child_rss": sum(item[3] for item in descendants),
        "child_data": sum(item[4] for item in descendants),
        "child_pss": sum(child_pss_values) if all(value is not None for value in child_pss_values) else None,
        "child_private": sum(child_private_values) if all(value is not None for value in child_private_values) else None,
        "members": {pid_value: (processes[pid_value][5], processes[pid_value][6]) for pid_value in members},
        "count": len(members),
        "time": time.monotonic(),
    }

previous_cpu = {}
tree_cpu_accumulated = 0
child_cpu_accumulated = 0
first_boot_ticks = None
process_exited = False
while not samples or time.monotonic() < sample_deadline:
    sample = process_tree(pid)
    if sample is None:
        process_exited = True
        break
    if first_boot_ticks is None:
        with open("/proc/uptime", encoding="ascii") as uptime:
            first_boot_ticks = int(float(uptime.read().split()[0]) * ticks_per_second)
    for member_pid, (cpu_ticks, start_ticks) in sample["members"].items():
        key = (member_pid, start_ticks)
        if key in previous_cpu:
            delta = max(0, cpu_ticks - previous_cpu[key])
        elif start_ticks >= first_boot_ticks:
            delta = cpu_ticks
        else:
            delta = 0
        previous_cpu[key] = cpu_ticks
        tree_cpu_accumulated += delta
        if member_pid != pid:
            child_cpu_accumulated += delta
    sample["tree_cpu_ticks"] = tree_cpu_accumulated
    sample["child_cpu_ticks"] = child_cpu_accumulated
    samples.append(sample)
    if time.monotonic() < sample_deadline:
        time.sleep(sample_interval_ms / 1000.0)

if not samples:
    raise SystemExit("RBL Studio exited or could not be sampled.")
if process_exited or samples[-1]["root_state"] == "Z":
    raise SystemExit("RBL Studio exited during the resource sampling window.")

elapsed = max(0.001, samples[-1]["time"] - sample_start)
average_sample_interval_ms = (
    (samples[-1]["time"] - samples[0]["time"]) * 1000 / (len(samples) - 1)
    if len(samples) > 1
    else 0.0
)
tree_cpu_one_core = samples[-1]["tree_cpu_ticks"] / ticks_per_second / elapsed * 100.0
child_cpu_one_core = samples[-1]["child_cpu_ticks"] / ticks_per_second / elapsed * 100.0
cpu_one_core = max(0.0, tree_cpu_one_core - child_cpu_one_core)
rss_values = [sample["root_rss"] for sample in samples]
data_values = [sample["root_data"] for sample in samples]
tree_rss_values = [sample["tree_rss"] for sample in samples]
tree_data_values = [sample["tree_data"] for sample in samples]
child_rss_values = [sample["child_rss"] for sample in samples]
child_data_values = [sample["child_data"] for sample in samples]
root_pss_values = [sample["root_pss"] for sample in samples if sample["root_pss"] is not None]
root_private_values = [sample["root_private"] for sample in samples if sample["root_private"] is not None]
child_pss_values = [sample["child_pss"] for sample in samples if sample["child_pss"] is not None]
tree_pss_values = [sample["tree_pss"] for sample in samples if sample["tree_pss"] is not None]
child_private_values = [sample["child_private"] for sample in samples if sample["child_private"] is not None]
tree_private_values = [sample["tree_private"] for sample in samples if sample["tree_private"] is not None]
child_count_values = [sample["count"] - 1 for sample in samples]
if scenario in ("CompilerTask", "TestSuite") and max(child_count_values, default=0) < 1:
    raise SystemExit(f"The {scenario} hotkey did not produce an observable child process.")
print(f"Samples: {len(samples)}")
print(f"Sample interval requested: {sample_interval_ms} ms")
print(f"Average observed sample interval: {average_sample_interval_ms:.1f} ms")
print(f"Executable: {executable}")
print(f"Executable SHA-256: {executable_hash}")
print(f"IDE source-set fingerprint at measurement SHA-256: {source_fingerprint}")
print("Source fingerprint scope: selected IDE source files and Cargo manifests at measurement time; not embedded in executable")
print(f"Cargo package version: {cargo_version}")
print(f"Scenario: {scenario}")
print(f"Operating system: {platform.platform()}")
print(f"Working set / RSS average: {sum(rss_values) / len(rss_values) / 1024:.1f} MiB")
print(f"Working set / RSS peak: {max(rss_values) / 1024:.1f} MiB")
print(f"VmData average (approx.): {sum(data_values) / len(data_values) / 1024:.1f} MiB")
print(f"Child processes RSS average / peak: {sum(child_rss_values) / len(child_rss_values) / 1024:.1f} / {max(child_rss_values) / 1024:.1f} MiB")
print(f"Child processes VmData average (approx.): {sum(child_data_values) / len(child_data_values) / 1024:.1f} MiB")
print(f"IDE + child process tree RSS average / peak: {sum(tree_rss_values) / len(tree_rss_values) / 1024:.1f} / {max(tree_rss_values) / 1024:.1f} MiB")
print(f"IDE + child process tree VmData average (approx.): {sum(tree_data_values) / len(tree_data_values) / 1024:.1f} MiB")
print(f"PSS samples available: {len(tree_pss_values)}/{len(samples)}")
if root_pss_values:
    print(f"IDE PSS average / peak: {sum(root_pss_values) / len(root_pss_values) / 1024:.1f} / {max(root_pss_values) / 1024:.1f} MiB")
else:
    print("IDE PSS: unavailable (smaps_rollup could not be read)")
if root_private_values:
    print(f"IDE private pages average / peak: {sum(root_private_values) / len(root_private_values) / 1024:.1f} / {max(root_private_values) / 1024:.1f} MiB")
else:
    print("IDE private pages: unavailable")
if child_pss_values:
    print(f"Child processes PSS average / peak: {sum(child_pss_values) / len(child_pss_values) / 1024:.1f} / {max(child_pss_values) / 1024:.1f} MiB")
else:
    print("Child processes PSS: unavailable")
if tree_pss_values:
    print(f"IDE + child process tree PSS average / peak: {sum(tree_pss_values) / len(tree_pss_values) / 1024:.1f} / {max(tree_pss_values) / 1024:.1f} MiB")
else:
    print("IDE + child process tree PSS: unavailable")
if child_private_values:
    print(f"Child processes private pages average / peak: {sum(child_private_values) / len(child_private_values) / 1024:.1f} / {max(child_private_values) / 1024:.1f} MiB")
else:
    print("Child processes private pages: unavailable")
if tree_private_values:
    print(f"IDE + child process tree private pages average / peak: {sum(tree_private_values) / len(tree_private_values) / 1024:.1f} / {max(tree_private_values) / 1024:.1f} MiB")
else:
    print("IDE + child process tree private pages: unavailable")
print(f"Child process count average / peak: {sum(child_count_values) / len(child_count_values):.1f} / {max(child_count_values)}")
print(f"CPU, IDE one logical processor: {cpu_one_core:.2f}%")
print(f"CPU, children one logical processor: {child_cpu_one_core:.2f}%")
print(f"CPU, process tree one logical processor: {tree_cpu_one_core:.2f}%")
print(f"CPU, process tree total machine capacity: {tree_cpu_one_core / logical_processors:.2f}%")
print(f"Logical processors: {logical_processors}")
print(f"Sample window: {elapsed:.1f} s")
if max(tree_rss_values) / 1024 > int(os.environ.get("RBL_STUDIO_MEMORY_LIMIT_MIB", "512")):
    raise SystemExit("Observed IDE + child process tree RSS peak exceeded the configured memory limit.")
PY

if [[ -n "$automation_pid" ]]; then
    if ! wait "$automation_pid"; then
        cat "$fixture_dir/automation.log" >&2
        printf 'IDE workload automation failed for scenario %s.\n' "$SCENARIO" >&2
        exit 4
    fi
    cat "$fixture_dir/automation.log"
fi


if (( generated_fixtures )) && [[ "$SCENARIO" == "EditUndoRedo" ]]; then
    for document in "${documents[@]}"; do
        [[ "$(stat -c %s "$document")" -gt $((8 * 1024 * 1024)) ]] || {
            printf 'Edit/undo/redo workload did not persist edits to %s.\n' "$document" >&2
            exit 4
        }
    done
fi
if (( generated_fixtures )) && [[ "$SCENARIO" == "FindReplaceUndoRedo" ]]; then
    [[ "$(stat -c %s "${documents[0]}")" -lt $((16 * 1024 * 1024)) ]] || {
        printf 'Find/Replace All workload did not reduce the generated fixture size.\n' >&2
        exit 4
    }
fi
