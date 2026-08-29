ARG BUILDER_IMAGE=registry.registry.svc.cluster.local:5000/driscord-ci@sha256:5383b53accf77cca58430bc2b4a313fc00f9fe653ef5bf0332b29746a36f2130
ARG RUNTIME_IMAGE=registry.registry.svc.cluster.local:5000/driscord-signaling-runtime-base@sha256:16e979f564e55402f66834606a5cb7531ea9b3a67fafe0ef551a3c39648ad15e

FROM ${BUILDER_IMAGE} AS builder


WORKDIR /src
COPY . .

RUN cmake -S . -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_CORE=OFF \
        -DBUILD_SERVER=ON \
        -DBUILD_TESTS=OFF \
        -DBUILD_QT_CLIENT=OFF \
    && cmake --build /build --target driscord_server --parallel

FROM ${RUNTIME_IMAGE} AS runtime

COPY --from=builder /build/backend/signaling_server/driscord_server /usr/local/bin/driscord-server

EXPOSE 9001
EXPOSE 49160-49200/udp

ENTRYPOINT ["/usr/local/bin/driscord-server"]
