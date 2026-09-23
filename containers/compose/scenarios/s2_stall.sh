#!/usr/bin/env bash
# T-0029 scenario S2 (stall): SIGSTOP the hot worker A through the harness;
# the supervisor's bounded SIGCONT (DEC-0012 #3) must resume it, observed as
# worker_stalled followed by the supervisor stall-recovered event.
#
# Requires the scenario stack up (see common.sh). Exits 0 on success.
set -Eeuo pipefail
cd "$(dirname "$0")/../../.."
# shellcheck source=common.sh
source containers/compose/scenarios/common.sh

REPLAY="${1:-$(mktemp /tmp/s2_replay.XXXXXX.jsonl)}"

wait_healthy 20
hot_a="$(worker_pid 0)"
log "S2: stalling hot worker A (pid ${hot_a})"
perturb stall --target "${hot_a}" | tee -a "${REPLAY}" >/dev/null

wait_log '"event":"worker_stalled","worker":0' 10
# The supervisor issues its own bounded SIGCONT immediately on the alert;
# nothing re-stops the worker, so recovery is deterministic.
wait_log 'supervisor: stall recovered for physical 0 at epoch' 10
wait_log '"event":"worker_recovered","worker":0' 10
wait_healthy 20

require_replay_line "${REPLAY}" '"category":"stall"'
log "S2 PASS: stall detected, supervisor SIGCONT recovery observed, replay ${REPLAY}"
