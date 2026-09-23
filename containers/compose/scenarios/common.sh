# shellcheck shell=bash
# T-0029 shared scenario helpers. Sourced by the s*.sh scenario scripts;
# the stack must already be up with the scenario overlay
# (docker compose -f docker-compose.yml
#            -f containers/compose/scenarios/compose.perturb.yml
#            --profile perturb up --wait).

COMPOSE_LOG=()

compose() {
  docker compose -f docker-compose.yml \
    -f containers/compose/scenarios/compose.perturb.yml \
    --profile perturb "$@"
}

log() { printf '==> %s\n' "$*"; }
fail() { printf '!! %s\n' "$*" >&2; exit 1; }

supervisor_id() { compose ps -q supervisor; }

worker_pid() {
  compose exec -T supervisor cat "/run/safety-critical-ha/safety_crit_worker_$1.pid" \
    | tr -d '[:space:]'
}

# Runs a perturb action from the harness container; the JSON-lines record
# goes to stdout (callers tee it into the replay file).
perturb() { compose exec -T perturb safety-critical-ha perturb "$@"; }

supervisor_log() { compose logs supervisor 2>/dev/null; }

wait_log() {
  local pattern="$1" budget="$2" elapsed=0
  while ! supervisor_log | grep -q "$pattern"; do
    sleep 0.2
    elapsed=$((elapsed + 1))
    if [ "$elapsed" -ge $((budget * 5)) ]; then
      fail "timeout waiting for supervisor log pattern: $pattern"
    fi
  done
}

wait_healthy() {
  local budget="$1" elapsed=0
  while :; do
    local state
    state="$(docker inspect --format '{{if .State.Health}}{{.State.Health.Status}}{{else}}{{.State.Status}}{{end}}' \
      "$(supervisor_id)" 2>/dev/null || echo unknown)"
    [ "$state" = healthy ] && return 0
    sleep 0.5
    elapsed=$((elapsed + 1))
    if [ "$elapsed" -ge $((budget * 2)) ]; then
      fail "timeout waiting for healthy state (last=${state})"
    fi
  done
}

# Waits until the worker's pidfile points at a new, live process.
wait_new_live_pid() {
  local idx="$1" old_pid="$2" budget="$3" elapsed=0
  while :; do
    local new_pid
    new_pid="$(worker_pid "$idx" || true)"
    if [ -n "$new_pid" ] && [ "$new_pid" != "$old_pid" ] &&
       compose exec -T supervisor kill -0 "$new_pid" 2>/dev/null; then
      return 0
    fi
    sleep 0.2
    elapsed=$((elapsed + 1))
    if [ "$elapsed" -ge $((budget * 5)) ]; then
      fail "timeout waiting for replacement pid on logical ring $idx"
    fi
  done
}

require_replay_line() {
  local file="$1" pattern="$2"
  grep -q "$pattern" "$file" || fail "replay record missing: $pattern"
}
