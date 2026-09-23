#include "test_framework.hpp"

#include <csignal>
#include <string>
#include <system_error>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <vector>

#include "replay_compare.hpp"
#include "safety_crit/perturb/harness.hpp"
#include "safety_crit/perturb/replay_log.hpp"

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define SAFETY_CRIT_PERT_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define SAFETY_CRIT_PERT_SANITIZED 1
#endif
#endif
#ifndef SAFETY_CRIT_PERT_SANITIZED
#define SAFETY_CRIT_PERT_SANITIZED 0
#endif

namespace {

using namespace safety_crit::perturb;

pid_t spawn_sleeper() {
    const pid_t pid = ::fork();
    SAFETY_CRIT_ASSERT(pid >= 0);
    if (pid == 0) {
        // A stray child holding the test's stdout pipe would deadlock ctest.
        (void)::prctl(PR_SET_PDEATHSIG, SIGKILL);
        for (;;) {
            ::pause();
        }
    }
    return pid;
}

}  // namespace

SAFETY_CRIT_TEST_CASE(Replay, ParsesVersionedLogWithSchemaHeader) {
    const std::string log =
        "{\"schema\":1,\"ts\":100,\"category\":\"_header\",\"target\":0,\"params\":{}}\n"
        "{\"ts\":200,\"category\":\"crash\",\"target\":42,\"params\":{}}\n"
        "{\"ts\":300,\"category\":\"double-fault\",\"target\":7,"
        "\"params\":{\"second\":9}}\n";
    std::vector<ReplayEntry> entries;
    std::string error;
    SAFETY_CRIT_ASSERT(parse_replay_log(log, entries, error));
    SAFETY_CRIT_ASSERT(entries.size() == 3u);
    SAFETY_CRIT_ASSERT(entries[0].is_header);
    SAFETY_CRIT_ASSERT(!entries[1].is_header);
    SAFETY_CRIT_ASSERT(entries[1].category == "crash");
    SAFETY_CRIT_ASSERT(entries[1].target == 42);
    SAFETY_CRIT_ASSERT(entries[2].category == "double-fault");
    SAFETY_CRIT_ASSERT(entries[2].second_target == 9);
}

SAFETY_CRIT_TEST_CASE(Replay, RequiresSchemaHeaderAndKnownCategories) {
    std::vector<ReplayEntry> entries;
    std::string error;
    // Missing schema header.
    SAFETY_CRIT_ASSERT(!parse_replay_log(
        "{\"ts\":200,\"category\":\"crash\",\"target\":42,\"params\":{}}\n", entries, error));
    // Unknown category.
    SAFETY_CRIT_ASSERT(!parse_replay_log(
        "{\"schema\":1,\"ts\":1,\"category\":\"_header\",\"target\":0,\"params\":{}}\n"
        "{\"ts\":2,\"category\":\"teleport\",\"target\":1,\"params\":{}}\n",
        entries, error));
    // Unsupported schema version.
    SAFETY_CRIT_ASSERT(!parse_replay_log(
        "{\"schema\":2,\"ts\":1,\"category\":\"_header\",\"target\":0,\"params\":{}}\n",
        entries, error));
    // Malformed record.
    SAFETY_CRIT_ASSERT(!parse_replay_log(
        "{\"schema\":1,\"ts\":1,\"category\":\"_header\",\"target\":0,\"params\":{}}\n"
        "garbage\n",
        entries, error));
}

#if !SAFETY_CRIT_PERT_SANITIZED
SAFETY_CRIT_TEST_CASE(Replay, ReissuesRecordedSignalsWithRemap) {
    const pid_t victim = spawn_sleeper();
    const std::string log =
        "{\"schema\":1,\"ts\":1000000,\"category\":\"_header\",\"target\":0,\"params\":{}}\n"
        "{\"ts\":2000000,\"category\":\"crash\",\"target\":9999,\"params\":{}}\n";
    std::vector<ReplayEntry> entries;
    std::string error;
    SAFETY_CRIT_ASSERT(parse_replay_log(log, entries, error));
    std::error_code ec;
    SAFETY_CRIT_ASSERT(replay_entries(entries, {{9999, victim}}, ec));
    int status = 0;
    SAFETY_CRIT_ASSERT(::waitpid(victim, &status, 0) == victim);
    SAFETY_CRIT_ASSERT(WIFSIGNALED(status));
    SAFETY_CRIT_ASSERT(WTERMSIG(status) == SIGSEGV);
}
#endif

SAFETY_CRIT_TEST_CASE(Replay, WitnessComparisonSemantics) {
    using safety_crit::perturb::test::event_categories_from_log;
    using safety_crit::perturb::test::OwnershipSnapshot;
    using safety_crit::perturb::test::Witness;
    using safety_crit::perturb::test::witnesses_equal;
    using safety_crit::perturb::test::witnesses_equal_with_tolerance;

    const std::string log =
        "x \"event\":\"worker_running\",\"worker\":0 y\n"
        "\"event\":\"worker_idle\",\"worker\":2\n";
    const std::vector<std::string> categories = event_categories_from_log(log);
    SAFETY_CRIT_ASSERT(categories.size() == 2u);
    SAFETY_CRIT_ASSERT(categories[0] == "worker_running");
    SAFETY_CRIT_ASSERT(categories[1] == "worker_idle");

    Witness original;
    original.event_categories = {"worker_running", "worker_running", "worker_idle"};
    original.ownership = {{2u, 3u}, {1u, 2u}, {2u, 2u}};
    original.records_per_ring = {100u, 100u, 0u};
    Witness replayed = original;
    SAFETY_CRIT_ASSERT(witnesses_equal(original, replayed));

    replayed.event_categories.push_back("worker_crashed");
    SAFETY_CRIT_ASSERT(!witnesses_equal(original, replayed));  // order/extra event matters
    replayed = original;
    replayed.ownership[0].epoch = 4u;
    SAFETY_CRIT_ASSERT(!witnesses_equal(original, replayed));  // ownership must match
    replayed = original;
    replayed.records_per_ring[0] = 105u;
    SAFETY_CRIT_ASSERT(!witnesses_equal(original, replayed));  // exact contract
    SAFETY_CRIT_ASSERT(witnesses_equal_with_tolerance(original, replayed, 10u));  // tolerant
    SAFETY_CRIT_ASSERT(!witnesses_equal_with_tolerance(original, replayed, 4u));
}
