#!/bin/bash
#SBATCH --partition=GENOA
#SBATCH --nodes=4
#SBATCH --account=dssc
#SBATCH --ntasks-per-node=64
#SBATCH --cpus-per-task=1
#SBATCH --mem=500G
#SBATCH --time=00:30:00
#SBATCH --output=strong_scaling.out

module load singularity/4.3.1
module load openMPI/4.1.6

export PMIX_MCA_psec=native
export OMPI_MCA_btl_vader_single_copy_mechanism=none

cd ~/scratch
REAL_SCRATCH=$(pwd -P)
CONTAINER=~/scratch/sample_sort.sif
RESULTS_CSV="strong_scaling.csv"
touch "$RESULTS_CSV"

echo "=== Running Strong Scaling Experiment ==="
for number in 4 8 16 32 64 128 256; do
    echo "Running with $number MPI tasks..."
    export OMP_NUM_THREADS=1
    mpirun -np $number singularity exec -B /orfeo --pwd $REAL_SCRATCH $CONTAINER /app/sample_sort_mpi \
        --n 1000000000 \
        --where_save $RESULTS_CSV
done
for number in 4 8 16 32 64 128 256; do
    echo "Running with $number MPI tasks..."
    export OMP_NUM_THREADS=1
    mpirun -np $number singularity exec -B /orfeo --pwd $REAL_SCRATCH $CONTAINER /app/sample_sort_mpi \
        --n 1000000000 \
        --where_save $RESULTS_CSV
done
for number in 4 8 16 32 64 128 256; do
    echo "Running with $number MPI tasks..."
    export OMP_NUM_THREADS=1
    mpirun -np $number singularity exec -B /orfeo --pwd $REAL_SCRATCH $CONTAINER /app/sample_sort_mpi \
        --n 1000000000 \
        --where_save $RESULTS_CSV
done
for number in 4 8 16 32 64 128 256; do
    echo "Running with $number MPI tasks..."
    export OMP_NUM_THREADS=1
    mpirun -np $number singularity exec -B /orfeo --pwd $REAL_SCRATCH $CONTAINER /app/sample_sort_mpi \
        --n 1000000000 \
        --where_save $RESULTS_CSV
done
echo "=== Completed ==="