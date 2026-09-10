#ifndef UTILS_H
#define UTILS_H

#define _POSIX_C_SOURCE 200809L

#include <stddef.h>
#include <stdint.h>
// #include <mpi.h>

#define DEFAULT_NKEYS          (1000000ULL)
#define DEFAULT_NBUCKETS       (8u)
#define DEFAULT_OVERSAMPLE     (1ULL)
#define DEFAULT_SEED           (1ULL)
#define DEFAULT_DISTRIBUTION   "uniform"
#define DEFAULT_SORT           "merge"
#define DEFAULT_MERGING_STRAT  "bin"
#define DEFAULT_RADIX_BITS     (8u)
#define DEFAULT_PRINT_LIMIT    (0ULL)

// Select type based on compile-time flag
#ifdef USE_UINT32
    typedef uint32_t sort_key_t;
    #define MPI_SORT_KEY_T MPI_UINT32_T
    #define PRI_KEY PRIu32
    #define N_BITS 32
#else
    // Default to 64-bit
    typedef uint64_t sort_key_t;
    #define MPI_SORT_KEY_T MPI_UINT64_T
    #define PRI_KEY PRIu64
    #define N_BITS 64
#endif

typedef enum {
  DISTRIBUTION_UNIFORM,
  DISTRIBUTION_SKEWED,
  DISTRIBUTION_FEW_UNIQUE,
  DISTRIBUTION_SORTED,
  DISTRIBUTION_REVERSE,
  DISTRIBUTION_ALMOST_SORTED
} distribution_t;

typedef enum {
  MERGE_SORT,
  RADIX_SORT,
  QUICK_SORT
} base_sorting;

typedef enum {
  BASIC_ITERATIVE_KWM,
  BINARY_ITERATIVE_KWM,
  HEAP_DIRECT_KWM,
  TORUNAMENT_TREE_DIRECT_KWM
} merging_strategy;

/*
  Runtime options. Guess what?  nbuckets plays the role of the
  number of MPI processes in the future distributed implementation, but here it
  only controls how the single array is split into virtual chunks.
*/
typedef struct {
  size_t            nkeys;
  unsigned int      nbuckets;
  size_t            oversample;
  uint64_t          seed;
  distribution_t    distribution;
  char             *distribution_name;
  base_sorting      sorting;
  char             *sorting_name;
  int               radix_bits;
  merging_strategy  merging;
  char             *merging_name;
  size_t            print_limit;
} options_t;

/*
  Lightweight phase timings. 
*/
typedef struct {
  double generation;
  double signature;
  double local_sort;
  double sampling;
  double partitioning;
  double merging;
  double sort_verification;
  double signature_verification;
  double total;
} timing_t;

/*
  Order-independent diagnostic signature.  Sortedness is the main correctness
  check used by this baseline; the signature is only an additional guard against
  accidental loss or duplication of keys while experimenting with the bucket
  code.
*/
typedef struct {
  uint64_t sum;
  uint64_t xor_value;
} signature_t;

double wall_seconds (void);
void *malloc_array (size_t count, size_t element_size);
uint64_t splitmix64_next (uint64_t * restrict state);
uint64_t mix_key (sort_key_t key);
void generate_keys (sort_key_t * restrict keys, const size_t nkeys, const options_t * restrict options);
signature_t compute_signature (sort_key_t *keys, size_t nkeys);
int same_signature (signature_t a, signature_t b);
int verify_sorted (sort_key_t *keys, size_t nkeys, size_t *bad_index);
void print_key_prefix (sort_key_t *keys, size_t nkeys, size_t print_limit);

#endif /* UTILS_H */