#!/bin/bash
#SBATCH --partition=GENOA
#SBATCH --nodes=4
#SBATCH --account=dssc
#SBATCH --ntasks-per-node=16
#SBATCH --cpus-per-task=4
#SBATCH --mem=60G
#SBATCH --time=00:30:00
#SBATCH --output=weak_scaling.out

module load singularity/4.3.1
module load openMPI/4.1.6

export PMIX_MCA_psec=native
export OMPI_MCA_btl_vader_single_copy_mechanism=none

cd ~/scratch
REAL_SCRATCH=$(pwd -P)
CONTAINER=~/scratch/sample_sort.sif
RESULTS_CSV="weak_scaling.csv"
touch "$RESULTS_CSV"

echo "=== Running Weak Scaling Experiment ==="
for number in 4 8 16 32; do
    N=$((20000000 * number))
    echo "Running with $number MPI tasks and N=$N..."
    export OMP_NUM_THREADS=4
    mpirun -np $number singularity exec -B /orfeo --pwd $REAL_SCRATCH $CONTAINER /app/sample_sort_mpi \
        --n $N \
        --where_save $RESULTS_CSV
done
for number in 4 8 16 32; do
    N=$((20000000 * number))
    echo "Running with $number MPI tasks and N=$N..."
    export OMP_NUM_THREADS=4
    mpirun -np $number singularity exec -B /orfeo --pwd $REAL_SCRATCH $CONTAINER /app/sample_sort_mpi \
        --n $N \
        --where_save $RESULTS_CSV
done
for number in 4 8 16 32; do
    N=$((20000000 * number))
    echo "Running with $number MPI tasks and N=$N..."
    export OMP_NUM_THREADS=4
    mpirun -np $number singularity exec -B /orfeo --pwd $REAL_SCRATCH $CONTAINER /app/sample_sort_mpi \
        --n $N \
        --where_save $RESULTS_CSV
done
for number in 4 8 16 32; do
    N=$((20000000 * number))
    echo "Running with $number MPI tasks and N=$N..."
    export OMP_NUM_THREADS=4
    mpirun -np $number singularity exec -B /orfeo --pwd $REAL_SCRATCH $CONTAINER /app/sample_sort_mpi \
        --n $N \
        --where_save $RESULTS_CSV
done
echo "=== Completed ==="