############################################################
# Global arguments
############################################################

ARG UBUNTU_VERSION=24.04
ARG NODE_VERSION=20-bookworm-slim

# 构建期 DNS 绕过: 宿主机 /etc/resolv.conf 含 IPv6 link-local nameserver
# (如 fe80::1%wlp5s0) 时, Docker 内嵌 DNS (127.0.0.11) 无法向上游转发,
# 构建容器内解析任何域名都会报 "Temporary failure resolving"。
# 构建容器内 /etc/hosts 与 /etc/resolv.conf 均为只读, 无法直接改写。
# 因此仓库根目录的 hosts 文件通过
#   RUN --mount=type=bind,source=hosts,target=/etc/hosts ...
# 挂载进每个需要联网的 RUN(apt/git/cmake-FetchContent/npm/curl),
# 完全绕开 DNS, 确定性解析国内镜像。
# 镜像 IP 变更时只需更新 ./hosts 文件。


############################################################
# Stage 1: C backend build
############################################################

FROM ubuntu:${UBUNTU_VERSION} AS backend-build


ARG HTTP_PROXY
ARG HTTPS_PROXY
ARG ALL_PROXY

ARG GITHUB_PROXY=""


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
    --mount=type=bind,source=hosts,target=/etc/hosts \
    sed -i 's@archive.ubuntu.com@mirrors.tuna.tsinghua.edu.cn@g; s@security.ubuntu.com@mirrors.tuna.tsinghua.edu.cn@g; s@ports.ubuntu.com@mirrors.tuna.tsinghua.edu.cn@g' /etc/apt/sources.list.d/ubuntu.sources /etc/apt/sources.list 2>/dev/null || true \
    && rm -f /etc/apt/apt.conf.d/80proxy \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
    build-essential \
    gcc-14 \
    g++-14 \
    cmake \
    git \
    pkg-config \
    libuv1-dev \
    libyaml-dev \
    libsqlite3-dev \
    libssl-dev \
    libcurl4-openssl-dev \
    zlib1g-dev \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*



############################################################
# Compiler
############################################################

ENV CC=gcc-14 \
    CXX=g++-14



############################################################
# Git proxy
############################################################

RUN --mount=type=bind,source=hosts,target=/etc/hosts \
    git config --global http.sslVerify false \
    && git config --global http.version HTTP/1.1 \
    && git config --global http.lowSpeedLimit 0 \
    && if [ -n "${HTTP_PROXY}" ]; then \
    git config --global http.proxy ${HTTP_PROXY}; \
    git config --global https.proxy ${HTTPS_PROXY}; \
    fi \
    && if [ -n "${GITHUB_PROXY}" ]; then \
    git config --global url.${GITHUB_PROXY}.insteadOf https://github.com/; \
    fi




############################################################
# Build backend
############################################################

WORKDIR /src

COPY backend ./backend

# 预取 csilk 的 FetchContent 依赖(csilk 的 cmake/dependencies.cmake 会拉取 llhttp/yyjson/nghttp2)
RUN --mount=type=bind,source=hosts,target=/etc/hosts \
    mkdir -p /src/deps \
    && cd /src/deps \
    && (test -d csilk || git clone --depth 1 https://github.com/quintin-lee/csilk.git csilk) \
    && (test -d llhttp || git clone --depth 1 -b release/v9.2.1 https://github.com/nodejs/llhttp.git llhttp) \
    && (test -d yyjson || git clone --depth 1 -b 0.12.0 https://github.com/ibireme/yyjson.git yyjson) \
    && (test -d nghttp2 || git clone --depth 1 -b v1.61.0 https://github.com/nghttp2/nghttp2.git nghttp2)

RUN --mount=type=bind,source=hosts,target=/etc/hosts \
    cmake \
    -S backend \
    -B backend/build \
    -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_CSILK=/src/deps/csilk \
    -DFETCHCONTENT_SOURCE_DIR_LLHTTP=/src/deps/llhttp \
    -DFETCHCONTENT_SOURCE_DIR_YYJSON=/src/deps/yyjson \
    -DFETCHCONTENT_SOURCE_DIR_NGHTTP2=/src/deps/nghttp2 \
    && cmake \
    --build backend/build \
    --parallel $(nproc)

RUN strip backend/build/minefolio


# Download swagger-ui distribution (csilk share/swagger-ui contains only placeholders)
RUN --mount=type=bind,source=hosts,target=/etc/hosts \
    curl --noproxy '*' -fsSL -o /tmp/swagger-ui-dist.tgz \
        https://registry.npmmirror.com/swagger-ui-dist/-/swagger-ui-dist-5.32.6.tgz \
    && mkdir -p /tmp/swagger-ui-extract \
    && tar -xzf /tmp/swagger-ui-dist.tgz -C /tmp/swagger-ui-extract \
    && DIST_DIR=$(find /tmp/swagger-ui-extract -type d -name package | head -1) \
    && cp -r "$DIST_DIR"/* /src/deps/csilk/share/swagger-ui/ \
    && rm -rf /tmp/swagger-ui-dist.tgz /tmp/swagger-ui-extract




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

RUN --mount=type=bind,source=hosts,target=/etc/hosts \
    npm install --registry=${NPM_REGISTRY}

COPY frontend ./
COPY .iconify_cache /app/.iconify_cache

ENV PATH="/app/frontend/node_modules/.bin:$PATH"

RUN npm run build
############################################################
# Stage 3: nginx
############################################################

FROM nginx:alpine AS nginx

COPY nginx/minefolio.docker.conf /etc/nginx/conf.d/default.conf

COPY --from=frontend-build /app/frontend/dist /opt/minefolio/frontend/dist



############################################################
# Stage 4: Runtime
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

RUN --mount=type=bind,source=hosts,target=/etc/hosts \
    sed -i 's@archive.ubuntu.com@mirrors.tuna.tsinghua.edu.cn@g; s@security.ubuntu.com@mirrors.tuna.tsinghua.edu.cn@g; s@ports.ubuntu.com@mirrors.tuna.tsinghua.edu.cn@g' /etc/apt/sources.list.d/ubuntu.sources /etc/apt/sources.list 2>/dev/null || true \
    && rm -f /etc/apt/apt.conf.d/80proxy \
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
    /src/deps/csilk/share \
    /app/share

COPY --from=backend-build \
    /src/deps/csilk/share/swagger-ui \
    /src/deps/csilk/share/swagger-ui


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
    CMD curl -f http://127.0.0.1:8080/healthz || exit 1



############################################################
# Start
############################################################

ENTRYPOINT ["/usr/bin/tini","--"]


CMD ["/app/minefolio"]
