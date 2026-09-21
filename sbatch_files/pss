#!/bin/bash

#SBATCH --partition=GENOA
#SBATCH --nodes=4
#SBATCH --account=dssc
#SBATCH --ntasks-per-node=16
#SBATCH --cpus-per-task=4
#SBATCH --mem=30G
#SBATCH --output=o.out
#SBATCH --time=01:00:00

echo "========================"
echo "running on:       $(hostname)"
echo "job_id            $SLURM_JOB_ID"
echo "directory:        $(pwd)"
echo "date:             $(date)"
echo "========================"

cd $HOME/uni/Parallel-Sample-Sort/Parallel/

module load openMPI/5.0.5

# Compile the code
make clean
make

export OMP_PLACES=threads
export OMP_PROC_BIND=spread

make run

echo "========================"
echo "finished work"
echo "finish date:      $(date)"
echo "========================"
