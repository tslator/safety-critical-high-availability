# Chapter 14 — Running as a System

## Motivation

Everything so far has been components. This chapter is the composed whole:
what `docker compose up` actually establishes, how health is *defined* (not
just probed), what the restart policy promises, and a runbook for the moments
where you are the operator holding the smoking signal.

## The topology

The Compose file encodes one deliberate decision (T-0021, DEC-0011 #4/#8):
**the supervisor is the only long-lived service** — it forks the monitor and
workers A/B/C, so the whole fleet shares one container, one `/dev/shm`, one
runtime dir (`docker-compose.yml:2`). Two companions: the observability daemon
and, under an opt-in `perturb` profile, the fault harness.

Key wiring, line by line (`docker-compose.yml:9`):

| Setting | Value | Why |
|---------|-------|-----|
| `ipc: shareable` (supervisor) | — | Makes the IPC namespace joinable (`docker-compose.yml:37`) |
| `ipc: service:supervisor` (daemon) | — | Daemon sees the supervisor's `/dev/shm` with zero copies, no extra mount (`docker-compose.yml:85`) |
| `cap_add` | `SYS_NICE`, `SYS_PTRACE` | SCHED_FIFO priorities; harness signaling (`docker-compose.yml:31`) |
| `shm_size` | 64m | The region is ~65 KB; headroom is free |
| `restart: unless-stopped` | supervisor + daemon | Supervisor loss exits the container; the policy re-establishes the whole topology (`docker-compose.yml:5,38`) |
| `ports` | `127.0.0.1:${OBSERVABILITY_PORT:-8080}:8080` | Loopback only — an operator surface, never network-facing (`docker-compose.yml:97`) |
| `ha-runtime` volume | `/run/safety-critical-ha` | pidfiles + `events.jsonl` shared with the daemon (`docker-compose.yml:40`) |
| `depends_on: service_healthy` | daemon waits for supervisor | The daemon attaches at startup; ordering is an ordering guarantee, not a race (chapter 05) |

## Health is a claim, not a probe

The supervisor healthcheck does not ask the binary if it's fine — it checks
the *evidence* (`docker-compose.yml:44`):

```text
region object exists AND all three worker pidfiles exist
AND kill -0 succeeds for all three recorded pids
```

If any hot worker (or the standby) dies and the supervisor has *not* replaced
it, health goes red. A supervisor process that is alive but not recovering is
unhealthy — exactly the claim you want. The daemon healthcheck is the HTTP
face of chapter 12's posture: 200 while region attached + identity verifies +
all rings owned; 503 otherwise (`docker-compose.yml:99`).

Note the subtlety with `restart: unless-stopped` + healthcheck: a supervisor
that wedges (healthy=false for 3 intervals) is *not* auto-restarted by
compose alone — the healthcheck gates dependents and reports to you; killing
PID 1 is the deliberate act (S6 shows the policy then carries the rebuild).

## Bring it up

```console
$ OBSERVABILITY_PORT=18080 docker compose up --wait --no-build   # 18080 if host 8080 is busy
$ docker compose ps
NAME                                       STATUS                   PORTS
...observability-1    Up (healthy)   127.0.0.1:18080->8080/tcp
...supervisor-1       Up (healthy)

$ curl -s localhost:18080/health | jq .status
"ok"
$ containers/compose/failover-smoke.sh
==> failover smoke PASSED
```

The smoke script (chapter 04 ran it end-to-end) is the one-command proof the
runtime contract holds: SIGKILL hot A, wait for stabilization, verify healthy
+ `worker_crashed` in supervisor stdout + new pidfile live + region survived +
`/health` and `/metrics` expose `data_loss_events_total`.

## Operator runbook

**Health is red / not converging.**
`docker compose logs supervisor --since 5m` — look for the shutdown witness
line (`state=`, `a_records=`, `a_corruptions=`) and monitor alert JSON.
`docker inspect --format '{{.State.RestartCount}}' <supervisor>` distinguishes
"never restarted" from "flapping."

**`/health` says degraded.**
The daemon is fine; the region is not (supervisor restarting — expected
transient — or region object deleted). `curl /status | jq .rings` shows
`owned: false` per orphaned ring. If it persists beyond a supervisor restart,
check `/dev/shm` inside the container.

**`scheduling fallback (errno 1)` in logs.**
Missing `CAP_SYS_NICE` (cap dropped, or running outside the provided
compose). Functionally fine (fallback scheduler), but RT latency guarantees
(property 6, chapter 02) are off — fix before real-time claims apply.

**A ring is DEGRADED.**
S5-style double fault: one ring lost its owner with no promotable standby.
This is terminal for the supervisor's lifetime by design; recovery is a
supervisor restart (`kill 1` inside the container, policy rebuilds) — and
`data_loss_events_total` tells you whether anything was actually lost while
degraded.

**Reproducing a production-shaped incident.**
Take the S1 replay log, adjust the target remap, replay against a stack
matched to production config (`containers/compose/scenarios/s1_replay.sh`,
chapter 11). Fresh stack per scenario.

**Full teardown between experiments:**

```console
$ docker compose down --volumes --remove-orphans
```

## Known environment quirks

- Host port 8080 busy → `OBSERVABILITY_PORT=18080`. Do **not** override
  `DAEMON_URL`; in-container healthchecks correctly target 8080 regardless.
- S1 recovery timing spreads 41–105 ms on busy hosts (budget < 100 ms); a
  lone near-miss is a rerun, a trend is a signal.
- S6 kills the shared PID namespace — anything scripted after it needs a
  fresh stack.
- Native (non-container) runs always show the scheduling fallback; the
  compose stack applies real SCHED_FIFO priorities.

## What to look for

- **The Compose file is a safety document.** `ipc:`, `cap_add:`, `ports:`,
  and the healthcheck shell each encode a property from chapter 02.
- **Health = evidence.** Pidfiles + region + liveness beat "process exists."
- **The operator path is tested too.** Every runbook action above is a
  scenario or smoke script in CI.

## Further reading

- `docker-compose.yml` — 139 heavily commented lines.
- `docs/ARCHITECTURE.md`, `docs/DEVELOPMENT.md`.
- Next: [Appendix A — CLI Reference](A-cli-reference.md).
