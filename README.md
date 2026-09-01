# Parallel Sample Sort

---

This repository contains the implementation of the **Parallel Sample Sort**, as final exercise for the academic course *High Performance and Cloud Computing* attended during the second semester of the first year at the MS in *Data Science and Artificial Intelligence* at the *University of Studies of Trieste*.

---

## Problem Formulation

Sort a globally distributed array of $N$ keys (32-bit or 64-bit integers, or double-precision floats, choose and justifies), where each of the $P$ MPI processes initially holds an unsorted chunk of approximately $N/P$ elements. At the end, process $r$ must hold a sorted subarray such that all elements on process $r$ are less than or equal to all elements on process $r+1$.

**Verification**: every output element must appear exactly once, and the global sequence must be nondecreasing. The student writes a parallel verifier.

**Suggested sizes**: $N$ from $10^7$ to $10^9$ aggregate; per-process input size $N/P$ in the range that fits comfortably in DRAM (not just in cache — otherwise you are not really doing a sort, you are doing a benchmark of a small problem).

---

## Algorithm : Sample Sort

1. Each process sorts its local chunk in place.
2. Each process selects $P-1$ regularly spaced samples from its sorted chunk (regular sampling, not random — this gives much better load balance).
3. All samples are gathered ( `MPI_Allgather` ), sorted, and from them $P-1$ global pivots are selected.
4. Each process partitions its local data according to the pivots into $P$ buckets.
5. Each process exchanges buckets with every other process ( `MPI_Alltoall` for sizes, then `MPI_Alltoallv` for data).
6. Each process performs a $k$-way merge of the $P$ incoming sorted streams (they are already sorted because the local chunks were sorted in step 1 and the buckets preserve ordering inside).

---

## Structural Requirements

* **OpenMP**  for the local sort, and for the $k$-way merge where applicable.
* **MPI** for the global communication: `MPI_Allgather` of the samples, `MPI_Alltoal`l of the bucket sizes, `MPI_Alltoallv` of the bucket data.
* **Robust to Imbalance**  If the input is drawn from a non-uniform distribution (the student should test
with at least one skewed input: a Zipf, or a normal with heavy tails), the regular-sampling step still gives
quasi-balanced output, but the imbalance does not vanish. Measure max / min / mean of the final perprocess sizes and discuss.

---

## Optimisation hints

* **Choice of local sort** A radix sort beats a quicksort on uniform 32-bit keys by a factor that depends on the number of passes and the cache  behaviour; on doubles or 64-bit keys, this is no longer obvious. The student picks one, justifies it, and ideally tries two.

* **Cache behaviour of the local sort** Branch-rich sorts (quicksort) and branch-poor sorts (radix) have very different performance signatures. [optional, not covered in course] : Measure with `perf stat -e branch-misses`,cache-misses and explain.
* **All-to-all is the bottleneck** Show the bandwidth achieved versus the injection rate of the network. Fora node-internal benchmark, compare intra-node `MPI_Alltoallv` against an OpenMP "shared-memory
all-to-all".
* **Memory allocation** The size of each incoming bucket is not known a priori. The student must exchange sizes first with `MPI_Alltoall`, then allocate, then `MPI_Alltoallv`. Discuss the cost of this two-phase pattern and what could be done if memory pressure is a problem.
* **$k$-way merge** The naïve approach merges two sorted runs at a time, $O(PlogP)$ passes through the data. A tournament tree / heap-based $k$-way merge does it in a single pass but with extra branch overhead. Measure.
* **Imbalance vs replication** With regular sampling on a uniform input, the worst-case bucket size is bounded by roughly $2N/P$ (classical result). Verify the bound empirically and discuss when it can be violated.

---

## Scaling

* **Strong Scaling**: fix $N$, vary $P$. Sample sort has $O(NlogN/P)$ local work and $O(N)$ aggregate communication; communication will dominate eventually. Find the cross-over for your machine.
* **Weak Scaling**: fix $N/P$, vary $P$. Per-process work stays constant in the local sort phase; per-process communicated volume grows as $N(P-1)/P \to N$. So weak scaling is inherently bad at high $P$ — show this and discuss whether a different sort (e.g. histogram sort or hyperquicksort) would do better and why.

---

## Suggested oral discussion points

* If your input is already sorted, what does sample sort do, and is that optimal?
* The regular-sampling step picks $P^2$ samples globally. Why not pick $P$ or $P^3$? What is the role of the *oversampling* factor?
* Why is `MPI_Alltoallv` typically much slower per byte than `MPI_Alltoall` for the same total data volume?
* On a fat-tree network with $L$ levels, what is the lower bound on communication time for `MPI_Alltoall` of total volume $V$ per process? How close did you get?
* What happens to your sort if you replace *MPI_Alltoallv* with $O(P^2)$ point-to-point messages? Predict and measure.

---

## Docker File

Reference dockerfile can be found in the Lecture notes.

Notable addition: `libnuma-dev` sould be included so that the student can call `numa_alloc_onnode()` explicitly if testing fine-grained NUMA placement inside the container. The student should document whether NUMA topology is visible inside the Singularity namespace (it usually is, via `/sys/devices/system/node/`).

---

## Singularity definition file

Convert the Docker image to an Singularity `.sif` via the definition file below, or pull the Docker image directly:

```bash
# Option A: build from the definition file (preferred — gives full control)
singularity build heat.sif heat.def 
# Option B: convert from Docker Hub or local daemon
singularity build heat.sif docker-daemon://heat:1.0
```

Reference definition file (`heat.def`) can be found in the lecture notes.

---

## Scalability experiments — container vs. native

Sample sort's `MPI_Alltoallv` is acutely sensitive to network library injection
quality. The student must probe multiple aspects:

* **All-to-all bandwidth**: use `osu_alltoallv` from the OSU suite, both natively and inside the container, for message sizes from 1 KiB to 64 MiB. A correctly injected MPI library should give identical results; any gap indicates incomplete binding.
* **Collective algorithm selection**: OpenMPI selects different collective algorithms depending on process count, message size, and network topology. Inside a container, the topology detection may differ (e.g., if `hwloc` or `fi_info` produces different output). Document which algorithm OpenMPI selected in each case with `--mca coll_tuned_use_dynamic_rules 1 -v` .
* **Imbalanced input sensitivity**: run the skewed-distribution test both native and containerised. The load imbalance should be identical (it is a function of the data, not the execution environment), but wall-clock time may differ if collective algorithms react differently.

---

## Required Experiments

| Experiment | Fixed Parameter | What varies |
| - | - | - |
| Strong scaling - native | $N==10^9$, uniform | $P\in \{4, 8, 16, 32, 64 \}$ |
| Strong scaling - container | same | same |
| Weak scaling - native | $N/P = 2 \times 10^7$, uniform | $P\in \{4, 8, 16, 32 \}$ |
| Weak scaling - container | same | same |
| Skewed Input - native | $N = 10^8$, Zipf $\alpha = 1.1$ | $P\in \{8, 16, 32 \}$ |
| Weak scaling - native | same | same |
| OSU `osu_alltov` | 2-64 ranks, intra+inter node | message 1 KiB - 64 MiB |

---

## Suggested oral discussion points (container layer)

* Your container MPI and the host MPI may use different collective algorithms for `MPI_Alltoallv`. How would you detect this? Would it matter for correctness? Would it matter for performance?
* The OSU `osu_alltoallv` benchmark showed X% lower bandwidth inside the container compared with native. Which layer is responsible: the container filesystem namespace, the MPI library binding, or the network driver injection? How would you systematically narrow it down?
* Sample sort's weak scaling is inherently poor because communicated volume per process grows with $P$. If you ran the same `.sif` on a cluster with twice the network bandwidth, by how much would the weak-scaling efficiency improve at $P = 64$?
