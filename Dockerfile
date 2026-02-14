FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libboost-all-dev \
    libssl-dev \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

RUN pip3 install conan==1.64.1

WORKDIR /app
COPY . .

RUN mkdir -p build && cd build \
    && conan install .. --build=missing \
    && cmake .. \
    && cmake --build .
