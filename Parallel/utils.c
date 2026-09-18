#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <limits.h>

/*
   : ------------------------------------------------------ :
   :  MISCELLANEOUS UTILITIES                               :
   : ------------------------------------------------------ :
*/ 

/*
  Allocate an array with size checking.
*/
void *
malloc_array ( size_t count,        // number of array elements
                size_t element_size   // size of one element in bytes
		) {

  void *ptr;

  if (count != 0 && element_size > SIZE_MAX / count) {
    fprintf (stderr, "Allocation size overflow: %zu elements of %zu bytes\n", count, element_size);
    exit (EXIT_FAILURE);
  }

  if (count == 0) return NULL;

  ptr = malloc (count * element_size);

  if (ptr == NULL) {
    fprintf (stderr, "Failed to allocate %zu bytes\n", count * element_size);
    exit (EXIT_FAILURE);
  }

  return ptr;

}

/*
  A small SplitMix64 generator.  It is fast, reproducible, and reasonable for
  input generation in this baseline.
*/
uint64_t
splitmix64_next ( uint64_t * restrict state   // mutable generator state
		  ) {

  uint64_t z = (*state += UINT64_C (0x9e3779b97f4a7c15));
  z = (z ^ (z >> 30)) * UINT64_C (0xbf58476d1ce4e5b9);
  z = (z ^ (z >> 27)) * UINT64_C (0x94d049bb133111eb);
  return z ^ (z >> 31);

}

/*
  Mix a key into a pseudo-random-looking 64-bit value.
  The function is used for order-independent signatures so that mistakaes in
  accidental bucket-copy mistakes can be detected
*/
inline uint64_t
mix_key ( sort_key_t key   // key value to mix
	  ) {

  uint64_t x = (uint64_t) key + UINT64_C (0x9e3779b97f4a7c15);
  x = (x ^ (x >> 30)) * UINT64_C (0xbf58476d1ce4e5b9);
  x = (x ^ (x >> 27)) * UINT64_C (0x94d049bb133111eb);
  return x ^ (x >> 31);

}

/*
  Generate an input array.
  The skewed distribution deliberately creates many keys in a small low-value range,
  which is useful to introduce bucket imbalance and test whether regular sampling
  is enough effective 
*/
void
generate_keys (       sort_key_t * restrict keys,      // output key array
                const size_t                nkeys,     // number of keys to generate
                const options_t  * restrict options    // runtime options
	      )
{

  uint64_t state = options->seed;
  size_t i, nswaps, a, b;
  sort_key_t tmp;

  if (options->distribution == DISTRIBUTION_SORTED) {
    for (i = 0; i < nkeys; i++) keys[i] = (sort_key_t) i;
    return;
  }

  if (options->distribution == DISTRIBUTION_REVERSE) {
    for (i = 0; i < nkeys; i++) keys[i] = (sort_key_t) (nkeys - i);
    return;
  }

  if (options->distribution == DISTRIBUTION_ALMOST_SORTED) {
    for (i = 0; i < nkeys; i++) keys[i] = (sort_key_t) i;
    nswaps = nkeys / 100;
    if (nswaps == 0) nswaps = 1;
    for (i = 0; i < nswaps; i++) {
      a = (size_t) (splitmix64_next (&state) % (uint64_t) nkeys);
      b = (size_t) (splitmix64_next (&state) % (uint64_t) nkeys);
      tmp = keys[a]; keys[a] = keys[b]; keys[b] = tmp;
    }
    return;
  }

  if (options->distribution == DISTRIBUTION_FEW_UNIQUE) {
    for (i = 0; i < nkeys; i++) keys[i] = (sort_key_t) (splitmix64_next (&state) % UINT64_C (1024));
    return;
  }

  if (options->distribution == DISTRIBUTION_SKEWED) {
    uint64_t small_range = (uint64_t) (nkeys / 16 + 1);
    for (i = 0; i < nkeys; i++) {
      uint64_t r = splitmix64_next (&state);
      if ((r & UINT64_C (255)) < UINT64_C (230))
        keys[i] = (sort_key_t) (splitmix64_next (&state) % small_range);
      else
        keys[i] = (sort_key_t) splitmix64_next (&state);
    }
    return;
  }

  for (i = 0; i < nkeys; i++) keys[i] = (sort_key_t) splitmix64_next (&state);

}

/*
   : ------------------------------------------------------ :
   :  VERIFICATION UTILITIES                                :
   : ------------------------------------------------------ :
*/ 

/*
  Compute an order-independent signature of the array.
  Note: this does not prove that the output is a permutation of the input, but it is a cheap diagnostic
  to assess that the elements are the same
*/
signature_t
compute_signature ( sort_key_t   *keys,    // array to inspect
                    size_t        nkeys    // number of keys
		  )
{
  uint64_t sum = 0;
  uint64_t xor_value = 0;

  #pragma omp parallel for reduction(+:sum) reduction(^:xor_value)
  for (size_t i = 0; i < nkeys; i++) {
        uint64_t mixed = mix_key(keys[i]);
        sum += mixed;
        xor_value ^= mixed;
  }

  signature_t sig;
  sig.sum = sum;
  sig.xor_value = xor_value;

  return sig;
}

/*
  Compare two signatures.  Kept as a helper so the final verification printout
  reads cleanly.
*/
int
same_signature ( signature_t a,    // first signature
                 signature_t b     // second signature
)
{
  return a.sum == b.sum && a.xor_value == b.xor_value;
}


/*
  Check that the array is globally non-decreasing.
*/
int 
verify_sorted ( sort_key_t *keys,       // array to verify
                 size_t     nkeys,      // number of keys
                 size_t    *bad_index   // first failing index, if any
	      )
{
  size_t first_bad = nkeys; // this is surelly greater then any other possible value

  // this parallelization does not actually help if there are errors 
  // in the first N/P values of the array
  // anyway, assuming that the sorting algorithm works, 
  // this does make sense since it should 
  // potentially speed up the process by a factor of P
  // notice also : we cannot use pragma cancel here. why?
  // we are doing a min reduction on the indexes!!!
  // if we were to return from a process which is working in a
  // "taller" section of the array it could return the false minimum!
  #pragma omp parallel for schedule(static) reduction(min : first_bad)
  for (size_t i = 1; i < nkeys; i++) {
    if (keys[i - 1] > keys[i]) {
      if (i < first_bad) {
        first_bad = i;
      }
    }
  }

  if (first_bad < nkeys) {
    *bad_index = first_bad;
    return 0;
  }

  *bad_index = 0;
  return 1;

}

/*
  Print a short prefix of the sorted array.
  This is useful for small examples.
*/
void
print_key_prefix ( sort_key_t   *keys,        // sorted key array
                   size_t        nkeys,       // number of keys
                   size_t        print_limit  // number of keys to print
		 ){

  if (print_limit == 0) return;
  size_t nprint = (print_limit > nkeys) ? nkeys : print_limit;
  printf ("first_keys");
  for (size_t i = 0; i < nprint; i++) printf (" %" PRIu64, (uint64_t) keys[i]);
  printf ("\n");
  
}