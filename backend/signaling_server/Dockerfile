# The signalling server image is assembled from images that already contain
# everything the build needs, so no package index, source archive or upstream
# base is fetched while it is built.
#
#   BUILDER_IMAGE  the pinned CI toolchain (clang, ninja, CMake, Boost with its
#                  config package, and a git config that resolves the project's
#                  FetchContent dependencies through the internal supply store);
#   RUNTIME_IMAGE  the pinned runtime base (ca-certificates, libssl3, the
#                  unprivileged driscord user, ports and entrypoint).
#
# Both default to the homelab registry and are overridden by digest from the
# infrastructure build policy, which is what keeps a toolchain bump visible as
# a source change instead of silently altering an image.
ARG BUILDER_IMAGE=registry.registry.svc.cluster.local:5000/driscord-ci@sha256:d2b45f2a8e6584b38e507518d97f77c53fd6a04c926d120031adc52ca779c011
ARG RUNTIME_IMAGE=registry.registry.svc.cluster.local:5000/driscord-signaling-runtime-base@sha256:16e979f564e55402f66834606a5cb7531ea9b3a67fafe0ef551a3c39648ad15e

FROM ${BUILDER_IMAGE} AS builder

# The toolchain image rewrites the project's FetchContent dependencies to the
# anonymous, read-only supply store of the build cluster (/etc/gitconfig), so
# this stage needs neither a credential nor internet access; the pinned
# GIT_TAGs in CMakeLists.txt are what guarantees the sources.

WORKDIR /src
COPY . .

# The deployable configuration: no Google WebRTC, no Qt client, no tests.
RUN cmake -S . -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_CORE=OFF \
        -DBUILD_SERVER=ON \
        -DBUILD_TESTS=OFF \
        -DBUILD_QT_CLIENT=OFF \
    && cmake --build /build --target driscord_server --parallel

FROM ${RUNTIME_IMAGE} AS runtime

COPY --from=builder /build/backend/signaling_server/driscord_server /usr/local/bin/driscord-server

# Repeated from the runtime base so this file states the contract it relies on.
EXPOSE 9001
# Media rides this UDP range, not the signalling port. Publishing it 1:1 is
# necessary but not sufficient: under bridge networking the gathered host
# candidate still carries the container address, so DRISCORD_ICE_STUN_URLS has
# to stay set for the SFU to advertise a reachable server-reflexive candidate.
# See docs/deployment.md.
EXPOSE 49160-49200/udp

ENTRYPOINT ["/usr/local/bin/driscord-server"]
