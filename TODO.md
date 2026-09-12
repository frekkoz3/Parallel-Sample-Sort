# TODO LIST

## HPC

### Code Side [HPC]

* [ ] Implemententing new merging strategy
* [ ] Adding the MPI part
* [ ] Developing a simple analyzer and visualizer for the results (Python side)

### Experiment Side [HPC]

* [ ] Keys' size 32/64 bits : Radix vs Quick sort comparison
* [ ] Local Sort comparison (see also `perf stat -e branch-misses`)
* [ ] Merging Algorithms comparison
* [ ] Data Distribution comparison
* [ ] Bandwidth achieved versus the Injection Rate of the network
* [ ] Strong Scaling (fix $N$, vary $P$)
* [ ] Weak Scaling (fix $N/P$, vary $P$)

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
