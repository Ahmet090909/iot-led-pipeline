FROM arm32v7/debian:latest

RUN apt-get update && apt-get install -y \
    gcc \
    make \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

COPY . .

RUN gcc main.c -o main
