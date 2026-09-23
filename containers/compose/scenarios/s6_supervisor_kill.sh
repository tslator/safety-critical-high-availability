#!/usr/bin/env bash
# T-0029 scenario S6 (supervisor loss): terminate the container's PID 1
# supervisor through the harness (shared PID namespace) and verify the
# container exits and the `unless-stopped` restart policy rebuilds the whole
# topology (DEC-0011 #8, DEC-0012 #6). A container's init process filters
# un-caught SIGKILL from sibling processes on this runtime, so the harness
# uses the caught-SIGTERM path (`supervisor-exit`); the exit + policy-rebuild
# contract is identical. This confirms the deliberate single point of trust,
# it does not mitigate it (Phase 6 scope).
#
# Requires the scenario stack up (see common.sh). Exits 0 on success.
set -Eeuo pipefail
cd "$(dirname "$0")/../../.."
# shellcheck source=common.sh
source containers/compose/scenarios/common.sh

REPLAY="${1:-$(mktemp /tmp/s6_replay.XXXXXX.jsonl)}"

wait_healthy 20
before="$(docker inspect --format '{{.RestartCount}}' "$(supervisor_id)")"
log "S6: killing supervisor PID 1 (restart count before=${before})"
perturb supervisor-exit --target 1 | tee -a "${REPLAY}" >/dev/null

elapsed=0
while :; do
  running="$(docker inspect --format '{{.State.Running}}' "$(supervisor_id)" 2>/dev/null || echo false)"
  [ "${running}" = true ] && break
  sleep 0.5
  elapsed=$((elapsed + 1))
  [ "${elapsed}" -lt 60 ] || fail "supervisor container did not come back within 30 s"
done
wait_healthy 30

after="$(docker inspect --format '{{.RestartCount}}' "$(supervisor_id)")"
[ "${after}" -gt "${before}" ] || fail "restart policy did not fire (before=${before} after=${after})"

require_replay_line "${REPLAY}" '"category":"supervisor-exit"'
log "S6 PASS: container exited and policy-rebuilt (RestartCount ${before} -> ${after}), replay ${REPLAY}"
