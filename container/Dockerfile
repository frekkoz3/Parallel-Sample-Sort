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
# Allow OpenMPI to run as root within the container 
# even if it should be Docker default
ENV OMPI_ALLOW_RUN_AS_ROOT=1
ENV OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1

CMD ["/bin/bash"]