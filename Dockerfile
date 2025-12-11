FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Installeer build tools + git om pigpio uit bron te bouwen
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
 && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp

# Clone en build pigpio (werkt voor arm64 via QEMU)
RUN git clone https://github.com/joan2937/pigpio.git /tmp/pigpio && \
    cd /tmp/pigpio && \
    make && \
    make install && \
    ldconfig && \
    rm -rf /tmp/pigpio

WORKDIR /work1
COPY . .

# Produceer de executable expliciet op /app/mybinary
RUN mkdir -p /app && \
    gcc main.c -o /app/mybinary $(pkg-config --cflags --libs pigpio) || (echo "gcc failed" && ls -la /work1 && exit 1)

# Debug output (optioneel)
RUN ls -la /app || true