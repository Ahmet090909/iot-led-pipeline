FROM ubuntu:22.04

# Install tools
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    tzdata \
    vim \
    build-essential \
    git \
    cmake \
    net-tools \
    gdb \
    clang

# Set working directory
WORKDIR /work

# Copy code from your GitHub repo into container
COPY . .

# Compile your program
RUN gcc main.c -o main
