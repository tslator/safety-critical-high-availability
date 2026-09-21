#!/usr/bin/env bash
# T-0022 Phase 4 exit measurement: repeated crash recovery and fault-to-
# first-post-failover-output timing (DEC-0011 #5, G4.3).
#
# Runs the built safety-critical-ha supervisor N times with a hot-A crash
# injected by SIGKILL, captures the supervisor's own witness events (first
# post-failover record ms and shutdown summary), and prints a per-iteration
# row plus aggregate stats. Requires a locally built binary; runs outside
# Docker so timings reflect the host directly.
#
# Usage: scripts/phase4-failover-timing.sh [N] [BINARY]
#   N defaults to 5; BINARY defaults to the plain GoogleTest build path.
set -Eeuo pipefail

N="${1:-5}"
BIN="${2:-build/t0020-gtest/app/safety-critical-ha}"
RUNTIME_MS="${RUNTIME_MS:-1500}"
TICKS="${TICKS:-5000000}"
REGION_PREFIX="${REGION_PREFIX:-/t0022_timing}"
SUM_THRESHOLD_MS="${FAILOVER_SLA_MS:-100}"

if [[ ! -x "$BIN" ]]; then
  printf '!! binary not found or not executable: %s\n' "$BIN" >&2
  exit 2
fi

workspace="$(mktemp -d -t t0022_timing_XXXX)"
trap 'rm -rf "$workspace"' EXIT INT TERM

printf 'iteration\tfault_to_first_output_ms\ta_records\ta_corruptions\ta_first_post_failover\tstate\n'

times=()
for ((i = 1; i <= N; ++i)); do
  region="${REGION_PREFIX}_${i}"
  pid_dir="${workspace}/pid_${i}"
  mkdir -p "$pid_dir"
  # Fresh region every iteration: shm persists after processes exit, so a
  # stale ownership cell from a prior run would let verify_identity pass but
  # leave acknowledge_ownership broken.
  rm -f "/dev/shm${region}"
  out="${workspace}/out_${i}.log"

  "$BIN" supervisor --runtime-ms "$RUNTIME_MS" --ticks "$TICKS" \
    --region "$region" --pid-dir "$pid_dir" >"$out" 2>&1 &
  sup=$!

  # Wait for hot physical A to publish its pidfile (up to 2 s).
  for _ in $(seq 0 200); do
    if [[ -r "${pid_dir}/safety_crit_worker_0.pid" ]]; then break; fi
    sleep 0.01
  done
  [[ -r "${pid_dir}/safety_crit_worker_0.pid" ]] || { kill "$sup" 2>/dev/null; wait "$sup" 2>/dev/null; printf '!! iteration %d: hot A never published pidfile\n' "$i" >&2; exit 1; }

  hot_a="$(tr -d '[:space:]' < "${pid_dir}/safety_crit_worker_0.pid")"
  # Fault injection: SIGKILL hot physical A.
  kill -9 "$hot_a" 2>/dev/null || true

  wait "$sup" 2>/dev/null || true

  first_line="$(grep -oE 'first post-failover record observed in [0-9]+ ms' "$out" | head -1 || true)"
  ms="$(printf '%s' "$first_line" | grep -oE '[0-9]+' | head -1 || echo '')"
  summary="$(grep -E '^supervisor: shutdown ' "$out" | tail -1 || true)"
  a_records="$(printf '%s' "$summary" | sed -nE 's/.*a_records=([0-9]+).*/\1/p')"
  a_corruptions="$(printf '%s' "$summary" | sed -nE 's/.*a_corruptions=([0-9]+).*/\1/p')"
  a_first="$(printf '%s' "$summary" | sed -nE 's/.*a_first_post_failover=([01]).*/\1/p')"
  state="$(printf '%s' "$summary" | sed -nE 's/.*state=([0-9]+).*/\1/p')"

  if [[ -z "$ms" ]]; then
    printf '!! iteration %d: no first-post-failover timing emitted\n' "$i" >&2
    sed -n '1,80p' "$out" >&2
    exit 1
  fi

  printf '%d\t%s\t%s\t%s\t%s\t%s\n' "$i" "$ms" "$a_records" "$a_corruptions" "$a_first" "$state"
  times+=("$ms")

  "$BIN" --version >/dev/null  # no-op; keeps binary path referenced for shellcheck
  (rm -rf "$pid_dir")
done

# Aggregate stats.
IFS=$'\n' sorted=($(printf '%s\n' "${times[@]}" | sort -n)); unset IFS
count=${#times[@]}
sum=0
for t in "${times[@]}"; do sum=$((sum + t)); done
min="${sorted[0]}"
max="${sorted[$((count - 1))]}"
median_idx=$((count / 2))
median="${sorted[$median_idx]}"
avg=$((sum / count))
exceeded=0
for t in "${times[@]}"; do
  if (( t > SUM_THRESHOLD_MS )); then exceeded=$((exceeded + 1)); fi
done

printf '\n'
printf 'measured=%d threshold=%d ms min=%d ms median=%d ms max=%d ms avg=%d ms over-threshold=%d\n' \
  "$count" "$SUM_THRESHOLD_MS" "$min" "$median" "$max" "$avg" "$exceeded"
if (( exceeded > 0 )); then
  printf 'result=FAIL\n'
  exit 1
fi
printf 'result=PASS\n'
