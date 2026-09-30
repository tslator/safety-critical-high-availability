# Appendix A — CLI Reference

Single binary, role-per-subcommand. Usage grammar from `app/src/main.cpp:33`
(hand-rolled parsing, DEC-0009 #5 — no external CLI dependency). Exit codes:
**0** success, **2** usage error, **1** runtime failure.

```text
usage: safety-critical-ha --version
       safety-critical-ha worker --id <a|b|c> [--role hot|standby]
           [--logical-id <a|b|c>] [--generation N] [--corrupt-hook]
           [--ticks N] [--tick-interval-ms MS] [--budget-us US]
           [--region NAME] [--pid-dir DIR] [--event-log PATH]
       safety-critical-ha monitor [--interval-ms MS]
           [--stall-threshold-ms MS] [--region NAME]
           [--pid-dir DIR] [--polls N] [--event-log PATH]
       safety-critical-ha supervisor [--runtime-ms MS]
           [--stall-grace-ms MS] [--region NAME] [--pid-dir DIR]
           [--ticks N] [--event-log PATH]
       safety-critical-ha perturb <crash|stall|recover-stall|corrupt|
           double-fault|supervisor-kill|supervisor-exit> --target <pid>
           [--target2 <pid>] [--out <path>]
       safety-critical-ha replay <logfile> [--target-remap <from>=<pid>]
       safety-critical-ha ownership [--region NAME]
       safety-critical-ha observability [--listen HOST:PORT]
           [--region NAME] [--event-log PATH]
           [--poll-interval-ms MS] [--ticks N] [--once]
```

## `--version`

Prints version and compiler identification:

```text
safety-critical-ha version 0.1.0
Compiler: GNU 13.3.0
```

## `worker` — run one worker process

| Flag | Default | Meaning |
|------|---------|---------|
| `--id a\|b\|c` | **required** | Physical worker index (0/1/2) |
| `--role hot\|standby` | hot | Hot: tick and publish. Standby: idle until promoted |
| `--logical-id a\|b\|c` | by index | Logical ring assignment |
| `--generation N` | 1 | Process generation for ownership records |
| `--corrupt-hook` | off | Arm SIGUSR2 poison-next-slot hook (T-0027; test surface, never for production) |
| `--ticks N` | until stop | Bounded tick count |
| `--tick-interval-ms MS` | 10 | Pace between ticks |
| `--budget-us US` | 1000 | Per-tick CPU budget; exceeding flags `kOverrun`, never fatal |
| `--region NAME` | `/safety_crit_region` | Shared-memory region |
| `--pid-dir DIR` | build default | Where `safety_crit_worker_<idx>.pid` lives |
| `--event-log PATH` | none | Append lifecycle records (schema v1) |

Notes: scheduling fallback prints to stderr, non-fatal; SIGTERM/SIGINT stop
at the next tick boundary; exit line is the witness summary
`worker <idx> (<role>): N ticks, M overruns`. Chapter 08.

## `monitor` — run the health monitor

| Flag | Default | Meaning |
|------|---------|---------|
| `--interval-ms MS` | 50 | Poll interval |
| `--stall-threshold-ms MS` | 200 | RUNNING + frozen ring tail beyond this ⇒ `worker_stalled` |
| `--region NAME` | `/safety_crit_region` | Region to observe (read-only) |
| `--pid-dir DIR` | build default | Pidfiles for liveness |
| `--polls N` | until stop | Bounded poll count |
| `--event-log PATH` | none | Append alert records |

Emits the six-name alert vocabulary as JSON lines plus a final
`monitor_report`. Chapter 09.

## `supervisor` — run the whole fleet

| Flag | Default | Meaning |
|------|---------|---------|
| `--runtime-ms MS` | 0 = until SIGTERM | Bounded run for demos/CI |
| `--stall-grace-ms MS` | 200 | Between SIGCONT and SIGKILL escalation |
| `--region NAME` | `/safety_crit_region` | Region to create/verify |
| `--pid-dir DIR` | build default | Child pidfiles |
| `--ticks N` | per config | Tick cap propagated to workers |
| `--event-log PATH` | none | Consolidated event log (T-0034) |

Forks monitor + hot A/B + standby C; runs the stall ladder and crash
failover; ends with the shutdown witness line
(`state= a_records= a_corruptions= a_first_post_failover= ...`). Chapter 10.

## `perturb` — inject one fault (test facility; NEVER for production)

| Category | Signal shape |
|----------|--------------|
| `crash` | SIGSEGV at target (SIGKILL fallback) |
| `stall` | SIGSTOP |
| `recover-stall` | SIGCONT (bookkeeping; never replayed) |
| `corrupt` | SIGUSR2 at a `--corrupt-hook` worker |
| `double-fault` | crash `--target` and `--target2` |
| `supervisor-kill` | SIGKILL at PID 1 of the container |
| `supervisor-exit` | graceful supervisor exit |

`--out <path>` writes the JSON-lines replay log. Chapter 11.

## `replay` — re-run a recorded fault sequence

```console
$ safety-critical-ha replay /tmp/s1_replay.cuJmbW.jsonl --target-remap 8=8
```

Re-issues recorded actions in order with PID remapping. Chapter 11 (S1R).

## `ownership` — read-only ownership probe

```console
$ safety-critical-ha ownership --region /safety_crit_region
ownership 0 physical=0 epoch=2
ownership 1 physical=1 epoch=2
ownership 2 physical=2 epoch=2
```

## `observability` — the HTTP daemon

| Flag | Default | Meaning |
|------|---------|---------|
| `--listen HOST:PORT` | required (daemon mode) | Bind address |
| `--region NAME` | `/safety_crit_region` | Region to attach read-only |
| `--event-log PATH` | none | Log to tail for gap detection |
| `--poll-interval-ms MS` | build default | Region/log poll cadence |
| `--ticks N` | until stop | Bounded run |
| `--once` | — | Print one `/status` snapshot to stdout, exit |

Serves `/health`, `/metrics`, `/status`. Region loss mid-run ⇒ `degraded`,
never exits. Chapter 12.
