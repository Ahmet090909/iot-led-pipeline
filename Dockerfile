# Build environment that compiles pigpio from source and then compiles your main.c
# Produces /app/mybinary which the workflow extracts and deploys to your Pi.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install build tools and dependencies (no libpigpio-dev from distro)
RUN apt-get update && apt-get install -y --no-install-recommends \
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
    pkgconf \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp

# Clone and build pigpio from upstream source (works on arm64 under QEMU)
RUN git clone https://github.com/joan2937/pigpio.git /tmp/pigpio && \
    cd /tmp/pigpio && \
    make -j"$(nproc)" && \
    make install && \
    ldconfig && \
    rm -rf /tmp/pigpio

# Workdir for your project sources
WORKDIR /work1
COPY . .

# Ensure library headers/libs are findable, compile main.c into /app/mybinary
# We prefer explicit include/lib dirs rather than relying purely on pkg-config.
RUN mkdir -p /app && \
    echo "----- pkg-config output -----" && pkg-config --cflags --libs pigpio || true && \
    echo "----- /usr/local/include -----" && ls -la /usr/local/include || true && \
    echo "----- /usr/local/lib -----" && ls -la /usr/local/lib || true && \
    echo "----- compiling main.c -----" && \
    gcc -v main.c -o /app/mybinary -I/usr/local/include -L/usr/local/lib -lpigpio -lrt -lpthread -lm 2>&1 | tee /tmp/gcc-output.txt || (echo "GCC failed, see /tmp/gcc-output.txt" && cat /tmp/gcc-output.txt && ls -la /work1 && exit 1)

# Show resulting binary (debug)
RUN ls -la /app || true

# No CMD / ENTRYPOINT: image used only to extract the binary in CI