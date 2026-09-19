# syntax=docker/dockerfile:1.7

# Reproducibility note:
# These base-image digests were captured with platform-specific pulls:
#   docker pull --platform linux/amd64 debian:bookworm
#   docker pull --platform linux/amd64 debian:bookworm-slim
# Digest lookup used:
#   docker image inspect --format '{{index .RepoDigests 0}}' debian:bookworm
#   docker image inspect --format '{{index .RepoDigests 0}}' debian:bookworm-slim
# If platform target changes, refresh both digest pins accordingly.
# NOTE: Be mindful of the index used with RepoDigests, as it may not be the same as the digest used for the pull command.
# NOTE: arm64 builds are not supported at this time, as the build process requires x86_64 binaries for testing and installation.

FROM --platform=linux/amd64 debian:bookworm@sha256:813017f3d62be4b5891a7acca6a01bdcd4b8513daa81b1ab99d3a50385b26931 AS builder
ENV DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# Primary build compiler ("gcc-primary", DEC-0008 #7 / T-0007): Debian
# bookworm's supported default GCC 12, installed explicitly as the versioned
# package g++-12 (source: deb.debian.org bookworm/main) and selected via
# -DCMAKE_CXX_COMPILER. Exact version at the most recent validated rebuild
# (see NOTES.md, section "T-0007"):
#   g++-12 12.2.0-14+deb12u1
#   g++-12 (Debian 12.2.0-14+deb12u1) 12.2.0
# Debian bookworm and bookworm-backports ship no gcc-13/g++-13 package
# (verified 2026-09-19), so a GCC 13 pin would require a third-party toolchain
# repository, which the Phase 0 toolchain policy declines. Host and CI build
# with their distro-default GCC 13 (13.3.0 on the reference host; CI records
# g++ --version per run); that divergence from this container baseline is
# documented in docs/DEVELOPMENT.md, section "Compiler baseline". Verify the
# built image reports its compiler with: docker run --rm <image> --version
# Rebuild rule (DEC-0008 maintenance): re-record the package name, version,
# source, and --version output on every toolchain update.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        g++-12 \
        cmake \
        ninja-build \
        git \
        ca-certificates \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . /workspace

RUN g++-12 --version \
    && cmake -S . -B build/docker -G Ninja \
      -DCMAKE_CXX_COMPILER=g++-12 \
      -DSAFETY_CRIT_BUILD_TESTING=ON \
      -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest \
    && cmake --build build/docker --parallel \
    && ctest --test-dir build/docker --output-on-failure \
    && cmake --install build/docker --prefix /usr/local

FROM --platform=linux/amd64 debian:bookworm-slim@sha256:abd67ffcfa541b485a3dff59865ab629aa048a6c613e639d36e7456b0b229241 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        gdb \
        linux-perf \
        sysstat \
        procps \
        curl \
        strace \
        valgrind \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/local/bin/safety-critical-ha /usr/local/bin/safety-critical-ha

ENTRYPOINT ["/usr/local/bin/safety-critical-ha"]
CMD ["--version"]
