#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // MADV_DONTFORK / MADV_HUGEPAGE / shm_open feature-test macro
#endif

#include "safety_crit/shared_memory/shm_attach.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace safety_crit::shared_memory {
namespace {

constexpr mode_t kShmMode = 0666;
constexpr std::size_t kHugePageBytes = 2u * 1024u * 1024u;

// A usable POSIX shared-memory name is "/name" (at least two characters).
bool name_is_valid(const char* name) {
    return name != nullptr && name[0] == '/' && name[1] != '\0';
}

}  // namespace

const char* to_string(AttachError error) {
    switch (error) {
        case AttachError::kOk: return "ok";
        case AttachError::kInvalidName: return "invalid-name";
        case AttachError::kSizeMismatch: return "size-mismatch";
        case AttachError::kStaleIdentity: return "stale-identity";
        case AttachError::kShmOpenFailed: return "shm-open-failed";
        case AttachError::kFstatFailed: return "fstat-failed";
        case AttachError::kFtruncateFailed: return "ftruncate-failed";
        case AttachError::kMmapFailed: return "mmap-failed";
        case AttachError::kInitializeFailed: return "initialize-failed";
    }
    return "unknown";
}

SharedRegionHandle::SharedRegionHandle(void* mapping, int fd, std::string name)
    : mapping_(mapping), fd_(fd), name_(std::move(name)), error_(AttachError::kOk), errnum_(0) {}

SharedRegionHandle SharedRegionHandle::failure(AttachError error, int errnum) {
    SharedRegionHandle handle;  // inert: no mapping, no fd
    handle.error_ = error;
    handle.errnum_ = errnum;
    return handle;
}

SharedRegionHandle::~SharedRegionHandle() { detach(); }

SharedRegionHandle::SharedRegionHandle(SharedRegionHandle&& other) noexcept
    : mapping_(other.mapping_),
      fd_(other.fd_),
      name_(std::move(other.name_)),
      error_(other.error_),
      errnum_(other.errnum_) {
    other.mapping_ = nullptr;
    other.fd_ = -1;
    other.error_ = AttachError::kInvalidName;
    other.errnum_ = 0;
}

SharedRegionHandle& SharedRegionHandle::operator=(SharedRegionHandle&& other) noexcept {
    if (this != &other) {
        detach();
        mapping_ = other.mapping_;
        fd_ = other.fd_;
        name_ = std::move(other.name_);
        error_ = other.error_;
        errnum_ = other.errnum_;
        other.mapping_ = nullptr;
        other.fd_ = -1;
        other.error_ = AttachError::kInvalidName;
        other.errnum_ = 0;
    }
    return *this;
}

void SharedRegionHandle::detach() {
    if (mapping_ != nullptr) {
        ::munmap(mapping_, sizeof(SharedRegion));
        mapping_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

SharedRegionHandle SharedRegionHandle::create_or_open(const char* name) {
    if (!name_is_valid(name)) {
        return failure(AttachError::kInvalidName, 0);
    }

    const int fd = ::shm_open(name, O_RDWR | O_CREAT, kShmMode);
    if (fd < 0) {
        return failure(AttachError::kShmOpenFailed, errno);
    }

    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        const int e = errno;
        ::close(fd);
        return failure(AttachError::kFstatFailed, e);
    }

    // Create-vs-existing is decided by the object's size (DEC-0007 #3): zero
    // means this is a brand-new object to size and initialize; exactly one
    // region means an existing object to verify (never re-initialize); any
    // other size is a foreign/stale object and is rejected outright.
    const off_t expected = static_cast<off_t>(sizeof(SharedRegion));
    bool fresh = false;
    if (st.st_size == 0) {
        if (::ftruncate(fd, expected) != 0) {
            const int e = errno;
            ::close(fd);
            ::shm_unlink(name);  // roll back our half-created object
            return failure(AttachError::kFtruncateFailed, e);
        }
        fresh = true;
    } else if (st.st_size != expected) {
        ::close(fd);
        return failure(AttachError::kSizeMismatch, 0);
    }

    void* mapping = ::mmap(nullptr, sizeof(SharedRegion), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        const int e = errno;
        ::close(fd);
        if (fresh) {
            ::shm_unlink(name);
        }
        return failure(AttachError::kMmapFailed, e);
    }

    // Keep the region out of forked children's address space: the supervisor
    // forks workers, and a child inheriting a live mapping is a hazard. A child
    // that needs the region re-attaches by name (DEC-0007 #6).
    (void)::madvise(mapping, sizeof(SharedRegion), MADV_DONTFORK);
    // Hugepages are best-effort and only meaningful for large regions (the
    // current ~193 KiB region is below one hugepage, so this is a no-op today);
    // failure is never an attach failure.
    if (sizeof(SharedRegion) >= kHugePageBytes) {
        (void)::madvise(mapping, sizeof(SharedRegion), MADV_HUGEPAGE);
    }

    SharedRegion* region = static_cast<SharedRegion*>(mapping);
    if (fresh) {
        // Precondition holds: the pages were just mapped and no other thread
        // has observed them (the creator initializes before spawning peers).
        if (!initialize(*region)) {
            ::munmap(mapping, sizeof(SharedRegion));
            ::close(fd);
            ::shm_unlink(name);
            return failure(AttachError::kInitializeFailed, 0);
        }
    } else if (!verify_identity(*region)) {
        // Existing object whose magic/version do not match compiled-in
        // constants (foreign, stale, or not-yet-initialized). Quiescence-
        // independent, so live ring traffic cannot cause a spurious rejection.
        ::munmap(mapping, sizeof(SharedRegion));
        ::close(fd);
        return failure(AttachError::kStaleIdentity, 0);
    }

    return SharedRegionHandle(mapping, fd, std::string(name));
}

AttachError SharedRegionHandle::destroy(const char* name) {
    if (!name_is_valid(name)) {
        return AttachError::kInvalidName;
    }
    if (::shm_unlink(name) != 0) {
        if (errno == ENOENT) {
            return AttachError::kOk;  // idempotent: nothing to remove
        }
        return AttachError::kShmOpenFailed;
    }
    return AttachError::kOk;
}

}  // namespace safety_crit::shared_memory
