# TODO LIST

## HPC

### Code Side [HPC]

* [x] Implemententing new merging strategy
* [x] Implementing a parallel verifier
* [x] Adding the MPI part
* [x] Developing a simple analyzer and visualizer for the results (Python side)

### Experiment Side [HPC]

* [x] Keys' size 32/64 bits : Radix vs Quick sort comparison
* [x] Local Sort comparison (see also `perf stat -e branch-misses`)
* [x] Merging Algorithms comparison
* [x] Oversample Tuning
* [x] Data Distribution comparison
* [ ] Bandwidth achieved versus the Injection Rate of the network (osu)
* [x] Point-to-point instead of `MPI_Alltoallv`
* [x] Strong Scaling (fix $N$, vary $P$)
* [x] Weak Scaling (fix $N/P$, vary $P$)

## Cloud Computing

### Code Side [CC]

* [ ] Docker file
* [ ] Singularity file
* [ ] Setting up everything

### Experiment Side [CC]

* [ ] Strong Scaling
* [ ] Weak Scaling
* [ ] Skewed Input
* [ ] Osu
