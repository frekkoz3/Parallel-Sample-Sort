#!/bin/bash

#SBATCH --partition=GENOA
#SBATCH --nodes=4
#SBATCH --account=dssc
#SBATCH --ntasks-per-node=64
#SBATCH --cpus-per-task=1
#SBATCH --mem=500G
#SBATCH --output=point_to_point.out
#SBATCH --time=01:00:00

$SLURM_JOB_ID

echo "========================"
echo "running on:       $(hostname)"
echo "job_id            $SLURM_JOB_ID"
echo "directory:        $(pwd)"
echo "date:             $(date)"
echo "========================"

cd
cd uni/Parallel-Sample-Sort/Parallel/

module load openMPI/5.0.5

export OMP_PLACES=threads
export OMP_PROC_BIND=spread

make clean
make
make point_to_point_communication
make point_to_point_communication
make point_to_point_communication
make point_to_point_communication

echo "========================"
echo "finished work"
echo "finish date:      $(date)"
echo "========================"