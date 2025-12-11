FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# Debug / robust apt update + install (zonder 'pkgconf')
RUN set -eux; \
    echo "=== /etc/apt/sources.list ==="; cat /etc/apt/sources.list || true; \
    attempt=0; \
    until [ $attempt -ge 5 ]; do \
      apt-get update && break || { attempt=$((attempt+1)); echo "apt-get update failed, retry $attempt/5"; sleep 3; }; \
    done; \
    echo "=== apt-cache policy (short) ==="; apt-cache policy || true; \
    echo "=== Trying apt-get install (no pkgconf) ==="; \
    attempt=0; \
    until [ $attempt -ge 5 ]; do \
      apt-get install -y --no-install-recommends \
        build-essential \
        gcc \
        make \
        pkg-config \
        ca-certificates \
        git \
        wget \
        libtool \
        autoconf \
        automake \
      && break || { attempt=$((attempt+1)); echo "apt-get install failed, retry $attempt/5"; sleep 3; }; \
    done; \
    # If still failing, dump apt lists for debugging
    if ! dpkg -l | grep -E 'build-essential|gcc' >/dev/null 2>&1; then \
      echo "### dpkg -l top 100 ###"; dpkg -l | head -n 100 || true; \
      echo "### apt-cache showpkg pkg-config ###"; apt-cache showpkg pkg-config || true; \
      echo "### listing /var/lib/apt/lists ###"; ls -la /var/lib/apt/lists || true; \
      false; \
    fi; \
    rm -rf /var/lib/apt/lists/*

WORKDIR /tmp

# Build pigpio from source (keeps you independent of distro lib package)
RUN set -eux; \
    git clone https://github.com/joan2937/pigpio.git /tmp/pigpio; \
    cd /tmp/pigpio; \
    make -j"$(nproc)"; \
    make install; ldconfig; \
    rm -rf /tmp/pigpio

WORKDIR /work1
COPY . .

# Compile your program into /app/mybinary with diagnostics
RUN set -eux; mkdir -p /app; \
    echo "pkg-config --cflags --libs pigpio:"; pkg-config --cflags --libs pigpio || true; \
    echo "ls /usr/local/lib:"; ls -la /usr/local/lib || true; \
    gcc -v main.c -o /app/mybinary -I/usr/local/include -L/usr/local/lib -lpigpio -lrt -lpthread -lm 2>&1 | tee /tmp/gcc-output.txt || (echo "GCC failed - contents /tmp/gcc-output.txt:" && cat /tmp/gcc-output.txt && ls -la /work1 && exit 1)

RUN ls -la /app || true