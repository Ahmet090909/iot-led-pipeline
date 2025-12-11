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

# Gebruik pkg-config om de juiste flags te krijgen (veiligste optie)
RUN gcc main.c -o main $(pkg-config --cflags --libs pigpio)

# Debug: show what files exist after compiling
RUN ls -R /work1