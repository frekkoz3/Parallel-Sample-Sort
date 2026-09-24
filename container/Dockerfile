# Base Operating System
FROM ubuntu:24.04

# Install build dependencies, OpenMPI, and libnuma
# libnuma is useful if we will be able to do the numa study
RUN apt-get update && apt-get install -y \
    build-essential \
    openmpi-bin \
    libopenmpi-dev \
    libnuma-dev \
    && rm -rf /var/lib/apt/lists/*

# Compile OSU Micro-Benchmarks inside the container
RUN wget https://mvapich.cse.ohio-state.edu/download/mvapich/osu-micro-benchmarks-7.4.tar.gz && \
    tar -xzf osu-micro-benchmarks-7.4.tar.gz && \
    cd osu-micro-benchmarks-7.4 && \
    ./configure CC=mpicc CXX=mpicxx --prefix=/opt/osu && \
    make -j4 && make install && \
    cd / && rm -rf /osu-micro-benchmarks-7.4*

# Set working directory inside the container
WORKDIR /app

# Copy all .c and .h and the Makefile into the container
COPY . /app

# Compile
RUN make all

# Set required network and execution environment variables
# in particular this environment variable ensure to use the
# PMIX (Process Management Interface Exchanger) as the same
# of the system it will run on.
ENV PMIX_MCA_psec=native

CMD ["/bin/bash"]