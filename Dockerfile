FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    make \
    pkg-config \
    libpigpio-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /work1
COPY . .

# Zorg dat output altijd op /app/mybinary komt
RUN mkdir -p /app && \
    gcc main.c -o /app/mybinary $(pkg-config --cflags --libs pigpio)

# Debug: list files (optioneel, kan verwijderd worden later)
RUN ls -la /work1 || true
RUN ls -la /app || true