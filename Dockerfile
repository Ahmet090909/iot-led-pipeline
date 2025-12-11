# Stage 1: builder (compile pigpio + main)
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential gcc make pkg-config ca-certificates git wget libtool autoconf automake \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp
# build pigpio from source
RUN git clone https://github.com/joan2937/pigpio.git /tmp/pigpio && \
    cd /tmp/pigpio && \
    make -j"$(nproc)" && make install && ldconfig && rm -rf /tmp/pigpio

WORKDIR /work1
COPY . .

# compile your program and put binary in /app
RUN mkdir -p /app && \
    gcc main.c -o /app/mybinary -I/usr/local/include -L/usr/local/lib -lpigpio -lrt -lpthread -lm

# Stage 2: runtime (small, same libc)
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# only install runtime deps (kept small)
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# copy binary and required pigpio lib(s) from builder
COPY --from=builder /app/mybinary /app/mybinary
# copy pigpio libs installed to /usr/local/lib
COPY --from=builder /usr/local/lib/libpigpio* /usr/local/lib/
RUN ldconfig || true

WORKDIR /app
# Ensure executable
RUN chmod +x /app/mybinary

# Run by default (optional)
CMD ["/app/mybinary"]