#!/bin/bash

#SBATCH --partition=GENOA
#SBATCH --nodes=1
#SBATCH --account=dssc
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=30G
#SBATCH --output=finding-numa.out
#SBATCH --time=01:00:00

module load singularity/4.3.1
module load openMPI/4.1.6

singularity exec ~/scratch/sample_sort.sif ls -la /sys/devices/system/node/