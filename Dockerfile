# Builder image: compile pigpio from source, compile your program to /app/mybinary
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# Split update/install and add retries to work around transient network/DNS issues.
RUN set -eux; \
    attempt=0; \
    until [ $attempt -ge 5 ]; do \
      apt-get update && break || { attempt=$((attempt+1)); echo "apt-get update failed, retry $attempt/5"; sleep 5; }; \
    done; \
    echo "APT sources:"; cat /etc/apt/sources.list || true; \
    echo "Trying apt-get install..."; \
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
      && break || { attempt=$((attempt+1)); echo "apt-get install failed, retry $attempt/5"; sleep 5; }; \
    done; \
    # If install still failed, dump apt state for debugging and exit non-zero
    dpkg -l | head -n 50 || true; \
    rm -rf /var/lib/apt/lists/*

WORKDIR /tmp

# Clone and build pigpio from source
RUN set -eux; \
    git clone https://github.com/joan2937/pigpio.git /tmp/pigpio; \
    cd /tmp/pigpio; \
    make -j"$(nproc)"; \
    make install; ldconfig; \
    rm -rf /tmp/pigpio

WORKDIR /work1
COPY . .

# Compile your program into /app/mybinary, with diagnostics on failure
RUN set -eux; mkdir -p /app; \
    echo "pkg-config output:"; pkg-config --cflags --libs pigpio || true; \
    echo "Listing /usr/local/include and /usr/local/lib:"; ls -la /usr/local/include || true; ls -la /usr/local/lib || true; \
    echo "Compile main.c ..."; \
    gcc -v main.c -o /app/mybinary -I/usr/local/include -L/usr/local/lib -lpigpio -lrt -lpthread -lm 2>&1 | tee /tmp/gcc-output.txt || (echo "GCC failed - /tmp/gcc-output.txt:" && cat /tmp/gcc-output.txt && ls -la /work1 && exit 1)

# Debug: show binary
RUN ls -la /app || true

# No CMD: image used only for building and extracting the binary in CI