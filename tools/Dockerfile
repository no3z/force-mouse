# Cross-compiler for the Akai Force (ARMv7 hard-float, glibc 2.39 on firmware 3.9.x).
# Debian bookworm ships glibc 2.36, so everything built here runs on the Force.
# (bullseye is not used any more: its armhf security packages return 404.)
FROM debian:bookworm

RUN dpkg --add-architecture armhf \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
        gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf libc6-dev-armhf-cross \
        libdrm-dev:armhf libasound2-dev:armhf \
 && rm -rf /var/lib/apt/lists/*
