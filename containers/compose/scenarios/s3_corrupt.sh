#!/usr/bin/env bash
# T-0029 scenario S3 (memory corruption): SIGUSR2 the hot worker A through
# the harness (hook installed via the overlay env, T-0027); the ring's
# skip-and-count path must tolerate the poisoned slot, the supervisor must
# keep running with a_corruptions >= 1 in its shutdown witness.
#
# This scenario GRACEFULLY STOPS the supervisor at the end (SIGTERM to PID 1)
# to read the witness summary; the caller owns stack teardown.
#
# Requires the scenario stack up (see common.sh). Exits 0 on success.
set -Eeuo pipefail
cd "$(dirname "$0")/../../.."
# shellcheck source=common.sh
source containers/compose/scenarios/common.sh

REPLAY="${1:-$(mktemp /tmp/s3_replay.XXXXXX.jsonl)}"

wait_healthy 20
hot_a="$(worker_pid 0)"
log "S3: poisoning next slot of hot worker A (pid ${hot_a})"
perturb corrupt --target "${hot_a}" | tee -a "${REPLAY}" >/dev/null

# Give the worker one tick and the supervisor a few drain iterations.
sleep 1
hot_a_after="$(worker_pid 0)"
[ "${hot_a_after}" = "${hot_a}" ] || fail "worker restarted: corruption path must not crash it"
supervisor_log | grep -q 'state=6' && fail "supervisor entered failsafe on tolerated corruption"
wait_healthy 10

log "S3: stopping supervisor gracefully to read the witness summary"
compose exec -T supervisor kill -TERM 1 2>/dev/null || true
# The restart policy may bring a fresh supervisor up again immediately, so
# wait on the shutdown witness line itself, not on process liveness.
wait_log 'supervisor: shutdown state=' 20

corruptions="$(supervisor_log | grep -oE 'a_corruptions=[0-9]+' | tail -1 | cut -d= -f2)"
[ -n "${corruptions}" ] || fail "no shutdown witness found"
[ "${corruptions}" -ge 1 ] || fail "expected a_corruptions >= 1, got ${corruptions}"

require_replay_line "${REPLAY}" '"category":"corrupt"'
log "S3 PASS: poisoned slot tolerated (a_corruptions=${corruptions}), supervisor survived, replay ${REPLAY}"
