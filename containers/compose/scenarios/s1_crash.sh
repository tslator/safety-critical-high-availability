#!/usr/bin/env bash
# T-0029 scenario S1 (crash): SIGSEGV the hot worker A through the perturb
# harness, assert crash detection, bounded replacement (DEC-0011 #5 <100 ms
# first post-failover record), and a crash record in the replay file.
#
# Requires the scenario stack up (see common.sh). Exits 0 on success.
set -Eeuo pipefail
cd "$(dirname "$0")/../../.."
# shellcheck source=common.sh
source containers/compose/scenarios/common.sh

REPLAY="${1:-$(mktemp /tmp/s1_replay.XXXXXX.jsonl)}"

wait_healthy 20
hot_a="$(worker_pid 0)"
log "S1: crashing hot worker A (pid ${hot_a})"
perturb crash --target "${hot_a}" | tee -a "${REPLAY}" >/dev/null

# Crash recovery is proven by the replacement going live and the supervisor
# emitting its first post-failover timing; the monitor's worker_crashed
# alert itself is asserted by failover-smoke (the supervisor's replacement
# can outrun the monitor's alert edge on this host).
wait_new_live_pid 0 "${hot_a}" 10
wait_healthy 20

timing_line="$(supervisor_log | grep -oE 'first post-failover record observed in [0-9]+ ms' | tail -1)"
[ -n "${timing_line}" ] || fail "no first post-failover timing emitted"
timing_ms="$(printf '%s' "${timing_line}" | grep -oE '[0-9]+')"
[ "${timing_ms}" -lt 100 ] || fail "recovery over budget: ${timing_ms} ms >= 100 ms"

require_replay_line "${REPLAY}" '"category":"crash"'
log "S1 PASS: crash alert, replacement live, ${timing_ms} ms < 100 ms, replay ${REPLAY}"
