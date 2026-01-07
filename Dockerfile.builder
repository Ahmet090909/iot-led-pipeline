FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc-aarch64-linux-gnu \
    libc6-dev-arm64-cross \
    ca-certificates \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /work
COPY . .

# main.c staat in root
RUN aarch64-linux-gnu-gcc \
    main.c lib/cjson/cJSON.c \
    -I. -Ilib/cjson \
    -lpigpio -lrt -lpthread \
    -o /work/intersection_arm64
