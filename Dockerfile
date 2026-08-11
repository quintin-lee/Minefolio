# syntax=docker/dockerfile:1.7


############################################################
# Global arguments
############################################################

ARG UBUNTU_VERSION=24.04
ARG NODE_VERSION=20-bookworm-slim


############################################################
# Stage 1: C backend build
############################################################

FROM ubuntu:${UBUNTU_VERSION} AS backend-build


ARG HTTP_PROXY
ARG HTTPS_PROXY
ARG ALL_PROXY

ARG GITHUB_PROXY=https://ghproxy.net/https://github.com/


ENV DEBIAN_FRONTEND=noninteractive \
    HTTP_PROXY=${HTTP_PROXY} \
    HTTPS_PROXY=${HTTPS_PROXY} \
    ALL_PROXY=${ALL_PROXY} \
    http_proxy=${HTTP_PROXY} \
    https_proxy=${HTTPS_PROXY}



############################################################
# apt proxy
############################################################

RUN if [ -n "${HTTP_PROXY}" ]; then \
    echo "Acquire::http::Proxy \"${HTTP_PROXY}\";" \
    > /etc/apt/apt.conf.d/80proxy ; \
    fi



############################################################
# Build dependencies
############################################################

RUN --mount=type=cache,target=/var/cache/apt \
    apt-get update \
    && apt-get install -y --no-install-recommends \
    build-essential \
    gcc-14 \
    g++-14 \
    cmake \
    ninja-build \
    git \
    pkg-config \
    libuv1-dev \
    libyaml-dev \
    libsqlite3-dev \
    libssl-dev \
    libcurl4-openssl-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*



############################################################
# Compiler
############################################################

ENV CC=gcc-14 \
    CXX=g++-14



############################################################
# Git proxy
############################################################

RUN git config --global http.sslVerify false \
    && if [ -n "${HTTP_PROXY}" ]; then \
    git config --global http.proxy ${HTTP_PROXY}; \
    git config --global https.proxy ${HTTPS_PROXY}; \
    fi \
    && git config --global \
    url.${GITHUB_PROXY}.insteadOf \
    https://github.com/




############################################################
# Build backend
############################################################

WORKDIR /src

COPY backend ./backend

RUN cmake \
    -S backend \
    -B backend/build \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
    && cmake \
    --build backend/build \
    --parallel $(nproc)

RUN strip backend/build/minefolio




############################################################
# Stage 2: Frontend build
############################################################

FROM node:${NODE_VERSION} AS frontend-build


ARG HTTP_PROXY
ARG HTTPS_PROXY

ARG NPM_REGISTRY=https://registry.npmmirror.com


ENV HTTP_PROXY=${HTTP_PROXY} \
    HTTPS_PROXY=${HTTPS_PROXY}



WORKDIR /app/frontend
COPY frontend/package.json frontend/package-lock.json ./

RUN npm install

COPY frontend ./

RUN npm run build




############################################################
# Stage 3: Runtime
############################################################

FROM ubuntu:${UBUNTU_VERSION} AS runtime



#
# IMPORTANT:
# 不继承任何 build proxy
#

ENV DEBIAN_FRONTEND=noninteractive



############################################################
# Runtime packages
############################################################

RUN rm -f /etc/apt/apt.conf.d/80proxy \
    && apt-get clean \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
    libuv1 \
    libyaml-0-2 \
    libsqlite3-0 \
    libssl3 \
    libcurl4 \
    zlib1g \
    libnghttp2-14 \
    ca-certificates \
    tini \
    curl \
    && rm -rf /var/lib/apt/lists/*



############################################################
# Application
############################################################

WORKDIR /app



COPY --from=backend-build \
    /src/backend/build/minefolio \
    /app/minefolio



COPY --from=backend-build \
    /src/backend/build/config \
    /app/config



COPY --from=backend-build \
    /src/backend/build/sql \
    /app/sql



COPY --from=frontend-build \
    /app/frontend/dist \
    /app/frontend/dist



COPY --from=backend-build \
    /src/backend/build/_deps/csilk-src/share/swagger-ui \
    /opt/csilk/share/swagger-ui


# NOTE: llhttp is built STATICALLY via FetchContent in the Docker build
# (csilk cmake/dependencies.cmake sets LLHTTP_BUILD_STATIC_LIBS ON when no
# system llhttp is found), so the runtime image needs no libllhttp shared lib.

############################################################
# User
############################################################

RUN mkdir -p /app/data \
    && useradd \
    --system \
    --uid 10001 \
    minefolio \
    && chown -R minefolio:minefolio /app



USER minefolio



############################################################
# Environment
############################################################

ENV MINEFOLIO_DB_DSN=/app/data/minefolio.db



EXPOSE 8080



############################################################
# Healthcheck
############################################################

HEALTHCHECK \
    --interval=30s \
    --timeout=5s \
    --start-period=10s \
    CMD curl -f http://127.0.0.1:8080/health || exit 1



############################################################
# Start
############################################################

ENTRYPOINT ["/usr/bin/tini","--"]


CMD ["/app/minefolio"]
