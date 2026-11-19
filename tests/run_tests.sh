#!/usr/bin/env bash
# CampusCompute smoke tests - run with:  make test
# Each test runs the real program and checks its exit code and key output.
# Expected scheduling numbers were derived independently by hand / script from data/jobs.txt.

cd "$(dirname "$0")/.." || exit 1
BIN=./campuscompute
pass=0
fail=0

# check <name> <expected exit code> <egrep pattern that must appear in stdout+stderr> <command...>
check() {
    local name="$1" want_rc="$2" pattern="$3"
    shift 3
    local out rc
    out=$("$@" 2>&1)
    rc=$?
    if [ "$rc" -eq "$want_rc" ] && echo "$out" | grep -Eq -- "$pattern"; then
        printf '  [PASS] %s\n' "$name"
        pass=$((pass + 1))
    else
        printf '  [FAIL] %s (exit %s, wanted %s; pattern: %s)\n' "$name" "$rc" "$want_rc" "$pattern"
        fail=$((fail + 1))
    fi
}

echo "CampusCompute smoke tests"
[ -x "$BIN" ] || { echo "Build first: make"; exit 1; }

echo "-- CLI and error handling (FR8)"
check "help text"                      0 "Usage: \./campuscompute"                $BIN help
check "unknown command rejected"       2 "unknown command 'foo'"                  $BIN foo
check "no command rejected"            2 "no command given"                       $BIN
check "bad quantum rejected"           2 "invalid time quantum '0'"               $BIN sched rr -q 0
check "malformed job file rejected"    1 "burst must be an integer.*'-4'"         $BIN sched fcfs -f data/jobs_bad.txt
check "duplicate job id reported"      1 "duplicate job id 1"                     $BIN sched fcfs -f data/jobs_bad.txt
check "missing job file reported"      1 "cannot open job file"                   $BIN sched fcfs -f no_such_file.txt
check "bad banker file rejected"       1 "exceeds Maximum"                        $BIN banker -f data/banker_bad.txt

echo "-- CPU scheduling (FR1, FR2)"
check "FCFS averages"                  0 "waiting time    : 11\.40"                $BIN sched fcfs
check "FCFS order"                     0 "J1 -> J2 -> J3 -> J4 -> J5"             $BIN sched fcfs
check "RR q=3 averages"                0 "waiting time    : 14\.60"                $BIN sched rr -q 3
check "RR q=3 order"                   0 "J1 -> J2 -> J3 -> J4 -> J1 -> J5 -> J2 -> J3 -> J4 -> J1 -> J3" $BIN sched rr -q 3
check "RR large quantum == FCFS"       0 "waiting time    : 11\.40"                $BIN sched rr -q 100
check "Priority averages"              0 "waiting time    : 9\.20"                 $BIN sched priority
check "Priority tie-break (J4 before J5)" 0 "J1 -> J2 -> J4 -> J5 -> J3"          $BIN sched priority
check "idle CPU gap handled"           0 "\| idle"                                $BIN sched fcfs -f data/jobs_gap.txt
check "comparison table"               0 "Lowest average waiting time.*Priority"  $BIN sched compare -q 3

echo "-- Concurrent logger (FR3)"
check "logger: nothing lost"           0 "18 / 18 / 18 / 18"                      $BIN logger -l /tmp/cc_test.log
check "logger: tiny queue still works" 0 "RESULT: PASS"                           $BIN logger -p 4 -c 3 -n 5 -s 1 -l /tmp/cc_test.log
check "logger: bad option rejected"    2 "invalid value '0' for -p"               $BIN logger -p 0
rm -f /tmp/cc_test.log

echo "-- Shared memory + semaphore IPC (FR4)"
check "ipc exchange and cleanup"       0 "checksum failures +: 0"                 $BIN ipc -m 3
check "ipc leaves no objects"          0 "IPC objects left behind +: 0"           $BIN ipc -m 2
if ls /dev/shm 2>/dev/null | grep -q '^cc_campus_\|^sem\.cc_campus_'; then
    printf '  [FAIL] no cc_campus_* objects remain in /dev/shm\n'; fail=$((fail + 1))
else
    printf '  [PASS] no cc_campus_* objects remain in /dev/shm\n'; pass=$((pass + 1))
fi

echo "-- Banker's algorithm (FR5)"
check "safe state + safe sequence"     0 "Safe sequence: <P1, P3, P4, P0, P2>"    $BIN banker safe
check "unsafe state identified"        0 "UNSAFE state"                           $BIN banker unsafe
check "need = max - allocation"        0 "P0 +\| 0 1 0 \| 7 5 3 \| 7 4 3"         $BIN banker safe

echo "-- Worker processes (FR6)"
check "normal workers"                 0 "normal completions \(status 0\) +: 2"   $BIN worker normal
check "simulated exit-status failure"  0 "NON-ZERO status 42.*\[SIMULATED\]"      $BIN worker fail
check "simulated signal crash"         0 "signal 9 \(Killed\).*\[SIMULATED\]"     $BIN worker crash
check "mixed scenario accurate"        0 "RESULT: PASS"                           $BIN worker all

echo "-- Resource limits and cleanup (FR7)"
check "RLIMIT_NOFILE effect"           0 "open\(\) failed: Too many open files"   $BIN rlimit nofile
check "RLIMIT_AS effect"               0 "malloc\(\) failed"                      $BIN rlimit mem
check "RLIMIT_CPU effect"              0 "SIGXCPU|CPU time limit exceeded"        $BIN rlimit cpu
check "cleanup verification"           0 "RESULT: PASS"                           $BIN cleanup

echo "-- Integrated demonstration"
check "demo passes all checks"         0 "Overall: 9 of 9 checks passed"          $BIN demo

echo
echo "Result: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
