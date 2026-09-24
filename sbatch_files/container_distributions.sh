#!/bin/bash

#SBATCH --partition=GENOA
#SBATCH --account=dssc
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=16
#SBATCH --cpus-per-task=4
#SBATCH --mem=60G
#SBATCH --time=01:00:00
#SBATCH --output=distributions.out

module load singularity/4.3.1
module load openMPI/4.1.6

export PMIX_MCA_psec=native
export OMPI_MCA_btl_vader_single_copy_mechanism=none
export OMP_NUM_THREADS=4

cd ~/scratch
REAL_SCRATCH=$(pwd -P)
CONTAINER=~/scratch/sample_sort.sif
RESULTS_CSV="distributions.csv"

touch "$RESULTS_CSV"

echo "=== Stress Testing Algorithm Distributions ==="
for run in {1..4}; do
    for dist in uniform skewed few-unique; do
        srun -n 16 -c 4 singularity exec -B /orfeo --pwd $REAL_SCRATCH $CONTAINER /app/sample_sort_mpi \
            --n 1000000000 \
            --distribution $dist \
            --where_save $RESULTS_CSV
    done
done

echo "========================"
echo "Finished work at: $(date)"
echo "========================"