FROM ubuntu:22.04

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential \
    gcc \
    make

WORKDIR /work1

COPY . .

RUN gcc main.c -o main -I. -L. -lpigpio

# Debug: show what files exist after compiling
RUN ls -R /work1
