#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "safety_crit/shared_memory/shared_region.hpp"

// Named /dev/shm attach/detach for a SharedRegion (Phase 1, T1.4).
//
// This header deliberately lives apart from shared_region.hpp and is the only
// place the library pulls POSIX system headers, so the core layout header stays
// free of <sys/mman.h>/<fcntl.h>/<unistd.h> for every consumer (DEC-0007 #1).
namespace safety_crit::shared_memory {

// One enumerator per attach failure mode. `kOk` means success. The attach path
// never throws; the code travels back through SharedRegionHandle::error().
enum class AttachError {
    kOk,
    kInvalidName,     // null, empty, or not of the form "/name"
    kSizeMismatch,    // existing object whose size != sizeof(SharedRegion)
    kStaleIdentity,   // existing object whose magic/version do not match
    kShmOpenFailed,   // shm_open
    kFstatFailed,     // fstat
    kFtruncateFailed, // ftruncate (fresh-object path only)
    kMmapFailed,      // mmap
    kInitializeFailed // in-place initialize() rejected the fresh region
};

const char* to_string(AttachError error);

// A mapped, verified SharedRegion — or the reason it could not be obtained.
// Move-only: exactly one handle owns the mapping, and its destructor munmaps
// (and closes the fd). It never shm_unlinks (peers may be attached; DEC-0007
// #4). Obtain one only via create_or_open().
class SharedRegionHandle {
public:
    SharedRegionHandle() = default;
    ~SharedRegionHandle();

    SharedRegionHandle(const SharedRegionHandle&) = delete;
    SharedRegionHandle& operator=(const SharedRegionHandle&) = delete;
    SharedRegionHandle(SharedRegionHandle&& other) noexcept;
    SharedRegionHandle& operator=(SharedRegionHandle&& other) noexcept;

    // Create-or-open a named region ("/name") of exactly sizeof(SharedRegion).
    //
    // Fresh object (size 0 after O_CREAT): ftruncate + mmap + madvise +
    // initialize() in place. Existing object (size == sizeof(SharedRegion)):
    // mmap + verify_identity() — never re-initialized. Any other nonzero size
    // is rejected (kSizeMismatch); a header that fails identity is rejected
    // (kStaleIdentity). On failure the returned handle is invalid and carries
    // error()/errnum(); on success ok() is true and get() points at the region.
    //
    // Does not block-wait for a concurrent creator: attach after the creator
    // finished initialize() (the supervisor creates before spawning workers).
    static SharedRegionHandle create_or_open(const char* name);

    // munmap + close the fd. Idempotent. Never shm_unlinks.
    void detach();

    // TEST-ONLY destructive path: shm_unlink(name), removing the named object.
    // Production code must never call this while peers may be attached; it
    // exists to exercise re-attachment and stale rejection (and perturbation S6).
    static AttachError destroy(const char* name);

    [[nodiscard]] bool ok() const { return error_ == AttachError::kOk && mapping_ != nullptr; }
    [[nodiscard]] AttachError error() const { return error_; }
    [[nodiscard]] int errnum() const { return errnum_; }
    [[nodiscard]] SharedRegion* get() const { return static_cast<SharedRegion*>(mapping_); }
    [[nodiscard]] const char* name() const { return name_.c_str(); }

private:
    // Fully-formed, valid handle (mapping != nullptr, error_ == kOk).
    SharedRegionHandle(void* mapping, int fd, std::string name);
    // Failed handle: nothing to release, error_ carries the reason.
    static SharedRegionHandle failure(AttachError error, int errnum);

    void* mapping_{nullptr};
    int fd_{-1};
    std::string name_;
    AttachError error_{AttachError::kInvalidName};
    int errnum_{0};
};

}  // namespace safety_crit::shared_memory
