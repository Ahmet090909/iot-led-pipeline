FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Basis build tools en vereisten
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

# Build en installeer pigpio uit bron (werkt op arm64/amd64)
RUN git clone https://github.com/joan2937/pigpio.git /tmp/pigpio && \
    cd /tmp/pigpio && \
    make && \
    make install && \
    ldconfig

# Cleanup build deps (optioneel)
RUN rm -rf /tmp/pigpio

WORKDIR /work1
COPY . .

# Zorg dat output altijd op /app/mybinary komt
RUN mkdir -p /app && \
    gcc main.c -o /app/mybinary $(pkg-config --cflags --libs pigpio) || (echo "gcc failed" && ls -la /work1 && exit 1)

# Debug: laat zien waar binary staat
RUN ls -la /app || true