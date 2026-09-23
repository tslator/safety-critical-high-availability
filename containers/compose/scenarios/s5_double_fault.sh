#!/usr/bin/env bash
# T-0029 scenario S5 (double fault): SIGKILL both hot workers with no gap
# through the harness. Physical C promotes to exactly one logical ring
# (lowest index, DEC-0012 #4); the other ring is DEGRADED exactly once and
# both replacement processes restart as standby (DEC-0011 #4 topology).
#
# Requires the scenario stack up (see common.sh). Exits 0 on success.
set -Eeuo pipefail
cd "$(dirname "$0")/../../.."
# shellcheck source=common.sh
source containers/compose/scenarios/common.sh

REPLAY="${1:-$(mktemp /tmp/s5_replay.XXXXXX.jsonl)}"

wait_healthy 20
hot_a="$(worker_pid 0)"
hot_b="$(worker_pid 1)"
log "S5: double-faulting hot workers A (pid ${hot_a}) and B (pid ${hot_b})"
perturb double-fault --target "${hot_a}" --target2 "${hot_b}" | tee -a "${REPLAY}" >/dev/null

wait_log 'logical ring 1 degraded (reason=standby_exhausted)' 10
degraded_events="$(supervisor_log | grep -c 'logical ring 1 degraded' || true)"
[ "${degraded_events}" = "1" ] || fail "expected exactly one DEGRADED event, got ${degraded_events}"
supervisor_log | grep -q 'logical ring 0 degraded' && fail "ring 0 must not degrade (C promotes there)"

# Both replacement physical processes restart as standby: new live pids.
wait_new_live_pid 0 "${hot_a}" 10
wait_new_live_pid 1 "${hot_b}" 10
wait_healthy 20

require_replay_line "${REPLAY}" '"category":"double-fault"'
require_replay_line "${REPLAY}" '"second":'
log "S5 PASS: one promotion + one DEGRADED, replacements live, replay ${REPLAY}"
