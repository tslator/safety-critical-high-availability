#!/usr/bin/env bash
# T-0030 scenario S1-R (deterministic replay): run S1 once, record the crash
# into a replay log, tear the stack down, rebuild it, replay the recorded log
# against the fresh stack (pid remap), and compare the witnesses.
#
# Determinism contract (DEC-0012 #8): identical supervisor event categories in
# identical order, identical final ownership tokens, and identical committed
# records per ring up to a small tolerance (record counts are timing-bound;
# events, ownership, and corruption counts are exact).
#
# Owns the full stack lifecycle: up -> S1 -> shutdown witness -> down ->
# up -> replay -> shutdown witness -> down. Exits 0 on success.
set -Eeuo pipefail
cd "$(dirname "$0")/../../.."
# shellcheck source=common.sh
source containers/compose/scenarios/common.sh

REPLAY="${1:-$(mktemp /tmp/s1_replay.XXXXXX.jsonl)}"
RECORDS_TOLERANCE=200

# Ordered supervisor event categories (timestamps/counts stripped; the
# categories themselves must match exactly, in order).
event_categories() {
  supervisor_log | grep -oE \
    'first post-failover record observed|logical ring [0-9]+ degraded|stall escalation|stall recovered' || true
}

ownership_snapshot() {
  compose exec -T supervisor safety-critical-ha ownership
}

witness_line() {
  supervisor_log | grep -E 'supervisor: shutdown state=' | tail -1
}

# Graceful shutdown of PID 1, wait for the witness line; the restart policy
# may bring a fresh supervisor up again, so wait on the line, not liveness.
shutdown_and_witness() {
  compose exec -T supervisor kill -TERM 1 2>/dev/null || true
  wait_log 'supervisor: shutdown state=' 20
}

run_original_phase() {
  wait_healthy 20
  local hot_a
  hot_a="$(worker_pid 0)"
  log "S1R[original]: crashing hot worker A (pid ${hot_a})"
  perturb crash --target "${hot_a}" | tee -a "${REPLAY}" >/dev/null
  wait_new_live_pid 0 "${hot_a}" 10
  wait_healthy 20
  # Snapshots BEFORE shutdown: the restart policy relaunches the supervisor
  # against a fresh /dev/shm region, which would reset ownership tokens.
  event_categories > /tmp/s1r_events_original.txt
  ownership_snapshot > /tmp/s1r_ownership_original.txt
  shutdown_and_witness
  witness_line > /tmp/s1r_witness_original.txt
}

run_replay_phase() {
  wait_healthy 20
  local hot_a replay_target remap
  hot_a="$(worker_pid 0)"
  # The recorded crash targets the ORIGINAL hot worker pid; remap it onto
  # this fresh stack's hot worker.
  replay_target="$(grep -m1 '"category":"crash"' "${REPLAY}" |
    grep -oE '"target":[0-9]+' | cut -d: -f2)"
  remap="${replay_target}=${hot_a}"
  log "S1R[replay]: replaying ${REPLAY} with target remap ${remap}"
  compose cp "${REPLAY}" supervisor:/tmp/s1r_replay.jsonl
  compose exec -T supervisor safety-critical-ha replay /tmp/s1r_replay.jsonl \
    --target-remap "${remap}"
  wait_new_live_pid 0 "${hot_a}" 10
  wait_healthy 20
  event_categories > /tmp/s1r_events_replayed.txt
  ownership_snapshot > /tmp/s1r_ownership_replayed.txt
  shutdown_and_witness
  witness_line > /tmp/s1r_witness_replayed.txt
}

compose down -v --remove-orphans >/dev/null 2>&1 || true

log "S1R phase 1: original run"
compose up --wait >/dev/null
run_original_phase
compose down -v --remove-orphans >/dev/null 2>&1 || true

log "S1R phase 2: replay against a fresh stack"
compose up --wait >/dev/null
run_replay_phase

# --- Compare witnesses -----------------------------------------------------
diff /tmp/s1r_events_original.txt /tmp/s1r_events_replayed.txt ||
  fail "event category sequences differ (original vs replayed above)"
diff /tmp/s1r_ownership_original.txt /tmp/s1r_ownership_replayed.txt ||
  fail "final ownership differs (original vs replayed above)"

# Crash must have been observed in BOTH runs.
grep -q 'first post-failover record observed' /tmp/s1r_events_replayed.txt ||
  fail "replay did not reproduce the crash event"
[ -s /tmp/s1r_events_original.txt ] || fail "original run emitted no categorized events"

orig_state="$(grep -oE 'state=[0-9]+' /tmp/s1r_witness_original.txt | cut -d= -f2)"
repl_state="$(grep -oE 'state=[0-9]+' /tmp/s1r_witness_replayed.txt | cut -d= -f2)"
[ "${orig_state}" = "${repl_state}" ] ||
  fail "shutdown states differ: ${orig_state} vs ${repl_state}"

orig_flags="$(grep -oE 'a_first_post_failover=[01]|failover_timing_emitted=[01]' /tmp/s1r_witness_original.txt | sort | tr '\n' ' ')"
repl_flags="$(grep -oE 'a_first_post_failover=[01]|failover_timing_emitted=[01]' /tmp/s1r_witness_replayed.txt | sort | tr '\n' ' ')"
[ "${orig_flags}" = "${repl_flags}" ] ||
  fail "witness failover flags differ: [${orig_flags}] vs [${repl_flags}]"

orig_records="$(grep -oE 'a_records=[0-9]+' /tmp/s1r_witness_original.txt | cut -d= -f2)"
repl_records="$(grep -oE 'a_records=[0-9]+' /tmp/s1r_witness_replayed.txt | cut -d= -f2)"
delta=$((orig_records - repl_records))
[ "${delta#-}" -le "${RECORDS_TOLERANCE}" ] ||
  fail "committed records differ beyond tolerance: ${orig_records} vs ${repl_records}"

orig_corruptions="$(grep -oE 'a_corruptions=[0-9]+' /tmp/s1r_witness_original.txt | cut -d= -f2)"
repl_corruptions="$(grep -oE 'a_corruptions=[0-9]+' /tmp/s1r_witness_replayed.txt | cut -d= -f2)"
[ "${orig_corruptions}" = "${repl_corruptions}" ] ||
  fail "corruption counts differ: ${orig_corruptions} vs ${repl_corruptions}"

compose down -v --remove-orphans >/dev/null 2>&1 || true
log "S1R PASS: events, ownership, and records match (a_records ${orig_records} vs ${repl_records}, tolerance ${RECORDS_TOLERANCE})"
