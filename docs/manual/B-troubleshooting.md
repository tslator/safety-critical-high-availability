# Appendix B — Troubleshooting

Observed failure modes, what they actually mean, and what to do. Each entry
was seen for real during the demos in this manual.

## Build problems

**`could not find requested CMake preset` / generator errors.**
The project expects `cmake -S . -B build/<dir> -G Ninja`. Ninja must be
installed; CMake >= 3.24. Check `cmake --version`.

**Tests "missing" or fewer than expected.**
The test target depends on the framework switch.
`-DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest` builds `*-tests` for gtest,
`Catch2` for Catch2. `ctest --test-dir build/gtest` runs gtest discovery;
the Catch2 build needs its own dir/run. 217 tests currently in the gtest
tree.

**Compile errors naming `std::atomic` or `<bit>`.**
You are below C++20. g++-12/13 or clang-14 required; clang leg also runs in
a pinned container (`clang-verify`).

## Runtime (native) problems

**`worker: scheduling fallback (errno 1)` on every start.**
`errno 1` = EPERM: SCHED_FIFO needs `CAP_SYS_NICE`. Expected and designed-for
outside privileged containers (chapter 02, property 6). Fallback scheduler
used; all functionality intact. Fix only if you need RT latency guarantees:
run via the compose stack, or grant the capability.

**`cannot attach` / attach rejection on first run.**
The region may not exist yet — workers attach-or-create, but the *supervisor*
path creates; a bare `monitor` against a never-created region correctly
refuses. Start a worker or supervisor first. A rejection with a size/identity
message means a stale `/dev/shm/<name>` object: remove it
(`rm /dev/shm/<name>`) only when you are certain no peer is attached — the
attach path's refusal is protecting you from exactly that mistake
(chapter 05).

**Stale pidfile warnings after crashes.**
Expected: pidfiles are removed on clean exit only; that is how the monitor
distinguishes crash from clean stop (chapter 09). Clear manually when the
fleet is down: `rm /tmp/pids/safety_crit_worker_*.pid`.

**Port 8080 already in use.**
`OBSERVABILITY_PORT=18080 ./run_demo.sh` (or compose). Do not override
`DAEMON_URL` — in-container healthchecks correctly target 8080.

## Scenario problems

**Scenario fails when run after another scenario.**
Run each scenario on a fresh stack:
`docker compose down --volumes --remove-orphans` first. S2 finds an already-
promoted standby; S6's PID-namespace kill takes the perturb sidecar with it —
both are shared-state artifacts, not bugs (chapter 11).

**`!! recovery over budget: 105 ms` in S1.**
Recovery latency spreads 41–105 ms on loaded hosts (CI: 36–96 ms; budget
100 ms). A lone near-miss on a busy machine: rerun on an idle host. A
consistent regression: bisect — this metric is property 5's acceptance test
and a real trend matters.

**Compose healthcheck never goes green.**
`docker compose logs supervisor` first. The supervisor healthcheck checks
region + 3 pidfiles + 3 live pids (chapter 14): a red check with a running
supervisor means a worker died and was not replaced — look for `DEGRADED`
(S5-shaped double fault) in the logs.

**`/health` returns 503 / `degraded`.**
Daemon is working; the region is not attached (supervisor mid-restart —
transient, expected) or rings are unowned (look at
`curl localhost:18080/status | jq '.rings[].owned'`). Persistent degraded
after a restart: check `/dev/shm/safety_crit_region` inside the supervisor
container.

**perturb: `Operation not permitted` signaling a target.**
Missing `CAP_SYS_PTRACE` on the perturb service, or the target is outside the
shared PID namespace. Use the compose-provided perturb service (the scenario
scripts do), not a host binary.

**corrupt injection does nothing.**
The target must have been started with `--corrupt-hook` (opt-in by design,
T-0027). SIGUSR2 to a worker without the armed hook is ignored — silently,
on purpose: production code paths carry no test surface.

## Reading failure *output* correctly

**`a_corruptions=1` on a clean-looking run.**
Someone injected a poison (S3) or memory really did disagree with its CRC.
Check the replay log — every harness action is recorded; if none matches,
that is *interesting* and worth a bug report.

**`event_log_gaps_total` climbing while `data_loss_events_total` is 0.**
Control-plane log writes are being lost (disk pressure, truncation), but the
data plane is intact. Different planes, different alarms — investigate the
filesystem, not the ring.

**Monitor reports `worker_idle` for a worker you expected running.**
`idle` includes clean exit and never-started-then-quiesced. Check the
pidfile (stale? fresh?) and the worker's last witness line. The standby is
legitimately idle until promoted (chapter 09).

**Container `RestartCount` climbing.**
The supervisor keeps dying. Each death exits the container; the policy
rebuilds it (chapter 10, S6). Read the *previous* container's logs
(`docker logs --previous`) for the shutdown witness line and last alerts.
