FROM debian:bookworm-slim AS builder

ARG BOOST_VERSION=1.89.0
ARG BOOST_VERSION_UNDERSCORE=1_89_0

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        g++ \
        git \
        libssl-dev \
        ninja-build \
        python3-pip \
        wget \
    && rm -rf /var/lib/apt/lists/* \
    && pip3 install --no-cache-dir --break-system-packages "cmake==3.31.6"

RUN wget -q "https://archives.boost.io/release/${BOOST_VERSION}/source/boost_${BOOST_VERSION_UNDERSCORE}.tar.gz" \
    && mkdir -p /opt/boost/include \
    && tar xzf "boost_${BOOST_VERSION_UNDERSCORE}.tar.gz" \
        --strip-components=1 \
        -C /opt/boost/include \
        "boost_${BOOST_VERSION_UNDERSCORE}/boost" \
    && rm "boost_${BOOST_VERSION_UNDERSCORE}.tar.gz"

WORKDIR /src
COPY . .

RUN cmake -S . -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBOOST_ROOT=/opt/boost \
        -DBUILD_CORE=OFF \
        -DBUILD_SERVER=ON \
        -DBUILD_TESTS=OFF \
        -DBUILD_QT_CLIENT=OFF \
    && cmake --build /build --target driscord_server --parallel

FROM debian:bookworm-slim AS runtime

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates libssl3 \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system driscord \
    && useradd --system --gid driscord --no-create-home driscord

COPY --from=builder /build/backend/signaling_server/driscord_server /usr/local/bin/driscord-server

USER driscord
ENV DRISCORD_PORT=9001
ENV DRISCORD_ICE_PORT_MIN=49160
ENV DRISCORD_ICE_PORT_MAX=49200
EXPOSE 9001
# Media rides this UDP range, not the signalling port. Publishing it 1:1 is
# necessary but not sufficient: under bridge networking the gathered host
# candidate still carries the container address, so DRISCORD_ICE_STUN_URLS has
# to stay set for the SFU to advertise a reachable server-reflexive candidate.
# See docs/deployment.md.
EXPOSE 49160-49200/udp

ENTRYPOINT ["/usr/local/bin/driscord-server"]
