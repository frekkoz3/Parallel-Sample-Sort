#!/bin/bash

#SBATCH --partition=GENOA
#SBATCH --nodes=1
#SBATCH --account=dssc
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=30G
#SBATCH --output=singularity_build.out
#SBATCH --time=00:30:00

# Load Singularity 4.3.1
module load singularity/4.3.1

# Navigate to scratch
cd ~/scratch

# Build SIF image from local tar archive
singularity build sample_sort.sif docker-archive://sample_sort_local.tar