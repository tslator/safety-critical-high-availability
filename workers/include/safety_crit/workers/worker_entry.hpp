#pragma once

#include "safety_crit/workers/pidfile.hpp"
#include "safety_crit/workers/worker_config.hpp"

namespace safety_crit::workers {

// Process-global default shared-memory region name (leading slash included:
// the name is passed verbatim to shm_open).
inline constexpr const char* kDefaultRegionName = "/safety_crit_region";

// Run one worker process to completion (T2.3, DEC-0009 #5/#6/#8; T3.2 adds
// the pidfile, DEC-0010 #2). Binds the T2.1 workload and the T2.2 loop to a
// real region:
//   attach  : SharedRegionHandle::create_or_open(region_name); failure is
//             reported on stderr and returns 1.
//   pidfile : write_worker_pidfile(pid_dir) right after attach (fatal on
//             failure: a live worker without a pidfile would look crashed
//             to the monitor); removed on clean exit only -- a crash
//             leaves the stale file for the monitor to reject.
//   hot     : status := RUNNING, run the work loop pushing ProcessedData
//             onto r->rings[worker_idx] via the region-level push (global_seq
//             + integrity_word maintained), then status := IDLE.
//   standby : status := IDLE; poll sibling status flags (read-only) until a
//             stop signal arrives; never touches any ring.
// SIGTERM/SIGINT stop the loop (flag consumed between ticks / pacing
// slices); SIGUSR1 is the crash hook (see signals.hpp). Returns the process
// exit code (0 on clean completion).
//
// Signal handlers are installed for the duration of the call and restored
// afterwards. The loop's status word is region.worker_status[worker_idx].
int run_worker(const WorkerConfig& cfg, const char* region_name,
               const char* pid_dir = kDefaultPidDir);

}  // namespace safety_crit::workers
