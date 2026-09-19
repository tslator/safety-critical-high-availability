#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // shm_open / fork / waitpid feature-test macro
#endif

#include "test_framework.hpp"

#include "safety_crit/shared_memory/shm_attach.hpp"

#include <cstddef>
#include <cstdint>

#include <fcntl.h>
#include <sys/mman.h>  // shm_open / shm_unlink declarations (via _GNU_SOURCE)
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

// ThreadSanitizer/AddressSanitizer do not reliably support fork(): forked
// children of a sanitized process hit runtime-fatal errors, and cross-process
// races are out of a sanitizer's per-process scope anyway. The fork-based
// re-attachment case therefore runs in the plain build (the G1.4 gate build);
// under the sanitizer matrix it degrades to a trivial pass and the same-process
// re-open paths above still exercise create-vs-existing and identity logic.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define SAFETY_CRIT_T14_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define SAFETY_CRIT_T14_SANITIZED 1
#endif
#endif
#ifndef SAFETY_CRIT_T14_SANITIZED
#define SAFETY_CRIT_T14_SANITIZED 0
#endif

namespace {
using namespace safety_crit::shared_memory;

// Raw, library-free creation of a /dev/shm object at a chosen size, so tests
// can present objects the attach path must reject (wrong size) or refuse to
// initialize (full size but foreign/uninitialized). Leaves pages zero-filled
// (tmpfs guarantees this).
void make_raw_object(const char* name, off_t size) {
    const int fd = ::shm_open(name, O_RDWR | O_CREAT, 0666);
    if (fd >= 0) {
        if (::ftruncate(fd, size) != 0) {
            // best effort; the test's assertions catch the resulting mismatch
        }
        ::close(fd);
    }
}
}  // namespace

SAFETY_CRIT_TEST_CASE(shm_attach, CreateRoundTrip) {
    const char* name = "/safety_crit_t14_roundtrip";
    SharedRegionHandle::destroy(name);

    SharedRegionHandle h = SharedRegionHandle::create_or_open(name);
    SAFETY_CRIT_ASSERT(h.ok());
    SAFETY_CRIT_ASSERT(h.get() != nullptr);
    SAFETY_CRIT_ASSERT(h.error() == AttachError::kOk);
    // Fresh region is self-consistent: identity stamped, every ring quiescent.
    SAFETY_CRIT_ASSERT(verify(*h.get()));
    SAFETY_CRIT_ASSERT(h.get()->identity.magic == kRegionMagic);
    SAFETY_CRIT_ASSERT(h.get()->identity.version == kRegionVersion);

    const std::uint64_t value = 0xABCD1234ull;
    SAFETY_CRIT_ASSERT(push(*h.get(), std::size_t{0}, value));

    // Re-open the now-existing object; it must not be re-initialized, so the
    // committed value is still present for a consumer on the second mapping.
    SharedRegionHandle h2 = SharedRegionHandle::create_or_open(name);
    SAFETY_CRIT_ASSERT(h2.ok());
    std::uint64_t out = 0;
    SAFETY_CRIT_ASSERT(h2.get()->rings[0].try_pop(out));
    SAFETY_CRIT_ASSERT(out == value);

    SAFETY_CRIT_ASSERT(SharedRegionHandle::destroy(name) == AttachError::kOk);
}

SAFETY_CRIT_TEST_CASE(shm_attach, ExistingOpenPreservesState) {
    const char* name = "/safety_crit_t14_existing";
    SharedRegionHandle::destroy(name);

    const std::uint64_t value = 0x5150u;
    {
        SharedRegionHandle h = SharedRegionHandle::create_or_open(name);
        SAFETY_CRIT_ASSERT(h.ok());
        SAFETY_CRIT_ASSERT(push(*h.get(), std::size_t{0}, value));
        // h detaches (munmap) at scope exit; the named object survives.
    }

    SharedRegionHandle reopened = SharedRegionHandle::create_or_open(name);
    SAFETY_CRIT_ASSERT(reopened.ok());
    SAFETY_CRIT_ASSERT(verify(*reopened.get()));
    SAFETY_CRIT_ASSERT(reopened.get()->identity.magic == kRegionMagic);
    SAFETY_CRIT_ASSERT(reopened.get()->identity.version == kRegionVersion);
    // Re-attachment recovers the exact producer position: the value is still
    // queued, proving the second open did not run initialize().
    std::uint64_t out = 0;
    SAFETY_CRIT_ASSERT(reopened.get()->rings[0].try_pop(out));
    SAFETY_CRIT_ASSERT(out == value);

    SAFETY_CRIT_ASSERT(SharedRegionHandle::destroy(name) == AttachError::kOk);
}

SAFETY_CRIT_TEST_CASE(shm_attach, RejectsWrongSize) {
    const char* name = "/safety_crit_t14_wrongsize";
    SharedRegionHandle::destroy(name);

    // A foreign object at the wrong (nonzero) size must be refused outright.
    make_raw_object(name, static_cast<off_t>(sizeof(SharedRegion) / 2));

    SharedRegionHandle h = SharedRegionHandle::create_or_open(name);
    SAFETY_CRIT_ASSERT(!h.ok());
    SAFETY_CRIT_ASSERT(h.error() == AttachError::kSizeMismatch);
    SAFETY_CRIT_ASSERT(h.get() == nullptr);

    SAFETY_CRIT_ASSERT(SharedRegionHandle::destroy(name) == AttachError::kOk);
}

SAFETY_CRIT_TEST_CASE(shm_attach, RejectsStaleIdentity) {
    // (a) full-size but uninitialized/zero pages => magic == 0 => stale.
    const char* zeroed = "/safety_crit_t14_stale_zero";
    SharedRegionHandle::destroy(zeroed);
    make_raw_object(zeroed, static_cast<off_t>(sizeof(SharedRegion)));
    {
        SharedRegionHandle h = SharedRegionHandle::create_or_open(zeroed);
        SAFETY_CRIT_ASSERT(!h.ok());
        SAFETY_CRIT_ASSERT(h.error() == AttachError::kStaleIdentity);
    }
    SAFETY_CRIT_ASSERT(SharedRegionHandle::destroy(zeroed) == AttachError::kOk);

    // (b) right magic but wrong version (a future-layout region) => stale.
    const char* wrongver = "/safety_crit_t14_stale_ver";
    SharedRegionHandle::destroy(wrongver);
    {
        SharedRegionHandle h = SharedRegionHandle::create_or_open(wrongver);
        SAFETY_CRIT_ASSERT(h.ok());
        h.get()->identity.version = kRegionVersion + 1;  // scribble via the map
    }
    {
        SharedRegionHandle reopened = SharedRegionHandle::create_or_open(wrongver);
        SAFETY_CRIT_ASSERT(!reopened.ok());
        SAFETY_CRIT_ASSERT(reopened.error() == AttachError::kStaleIdentity);
    }
    SAFETY_CRIT_ASSERT(SharedRegionHandle::destroy(wrongver) == AttachError::kOk);
}

SAFETY_CRIT_TEST_CASE(shm_attach, CrossProcessReattach) {
#if SAFETY_CRIT_T14_SANITIZED
    SAFETY_CRIT_ASSERT(true);  // fork skipped under sanitizers (see header note)
#else
    const char* name = "/safety_crit_t14_xproc";
    SharedRegionHandle::destroy(name);

    SharedRegionHandle h = SharedRegionHandle::create_or_open(name);
    SAFETY_CRIT_ASSERT(h.ok());
    const std::uint64_t value = 0x1122333344445555ull;
    SAFETY_CRIT_ASSERT(push(*h.get(), std::size_t{0}, value));

    const pid_t pid = ::fork();
    if (pid == 0) {
        // Child: MADV_DONTFORK means the parent's mapping is NOT inherited, so
        // the child must re-attach by name -- exactly the re-attachment path.
        SharedRegionHandle child = SharedRegionHandle::create_or_open(name);
        if (!child.ok()) {
            ::_exit(2);
        }
        std::uint64_t out = 0;
        if (!child.get()->rings[0].try_pop(out)) {
            ::_exit(3);
        }
        if (out != value) {
            ::_exit(4);
        }
        ::_exit(0);
    } else if (pid > 0) {
        int status = 0;
        ::waitpid(pid, &status, 0);
        SAFETY_CRIT_ASSERT(WIFEXITED(status));
        SAFETY_CRIT_ASSERT(WEXITSTATUS(status) == 0);
    } else {
        SAFETY_CRIT_ASSERT(false);  // fork() failed
    }

    SAFETY_CRIT_ASSERT(SharedRegionHandle::destroy(name) == AttachError::kOk);
#endif
}

SAFETY_CRIT_TEST_CASE(shm_attach, DestroyRemovesName) {
    const char* name = "/safety_crit_t14_destroy";
    SharedRegionHandle::destroy(name);

    {
        SharedRegionHandle h = SharedRegionHandle::create_or_open(name);
        SAFETY_CRIT_ASSERT(h.ok());
        SAFETY_CRIT_ASSERT(push(*h.get(), std::size_t{0}, 999ull));
    }

    // Destructive reset clears the name entirely...
    SAFETY_CRIT_ASSERT(SharedRegionHandle::destroy(name) == AttachError::kOk);
    // ...so the next open takes the fresh-create path (empty, re-initialized),
    // not the existing-object path.
    SharedRegionHandle fresh = SharedRegionHandle::create_or_open(name);
    SAFETY_CRIT_ASSERT(fresh.ok());
    SAFETY_CRIT_ASSERT(verify(*fresh.get()));
    SAFETY_CRIT_ASSERT(fresh.get()->rings[0].pushed() == 0);
    SAFETY_CRIT_ASSERT(fresh.get()->rings[0].consumed() == 0);
    std::uint64_t out = 0;
    SAFETY_CRIT_ASSERT(!fresh.get()->rings[0].try_pop(out));

    SAFETY_CRIT_ASSERT(SharedRegionHandle::destroy(name) == AttachError::kOk);
}
