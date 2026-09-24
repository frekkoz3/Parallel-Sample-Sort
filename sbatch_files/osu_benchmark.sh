#!/bin/bash

#SBATCH --partition=GENOA
#SBATCH --account=dssc
#SBATCH --nodes=1
#SBATCH --ntasks=16
#SBATCH --cpus-per-task=4
#SBATCH --mem=60G
#SBATCH --time=00:20:00
#SBATCH --output=osu_benchmark.out

module load singularity/4.3.1
module load openMPI/4.1.6

export PMIX_MCA_psec=native
export OMPI_MCA_btl_vader_single_copy_mechanism=none

cd ~/scratch
REAL_SCRATCH=$(pwd -P)
CONTAINER=~/scratch/sample_sort.sif

NATIVE_OSU=~/scratch/osu-micro-benchmarks-7.4/c/mpi/collective/blocking/osu_alltoallv
CONTAINER_OSU=/opt/osu/libexec/osu-micro-benchmarks/mpi/collective/blocking/osu_alltoallv

echo "Running Native OSU Alltoallv Benchmark"
srun -n 16 $NATIVE_OSU -m 1024:67108864 > ~/scratch/osu_native.out

echo "Running Containerized OSU Alltoallv Benchmark"
srun -n 16 singularity exec -B /orfeo --pwd $REAL_SCRATCH $CONTAINER $CONTAINER_OSU -m 1024:67108864 > ~/scratch/osu_container.out