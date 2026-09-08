#include "local_sort.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h> // debug
#include <omp.h>

/* 
   : ------------------------------------------------------ :
   : MERGE SORT                                             :
   : ------------------------------------------------------ :
*/ 

/*
  Compute the inclusive begin index of a virtual chunk.
  The first remainder chunks receive one extra element.
*/
size_t chunk_begin ( const size_t       nkeys,      // total number of keys
              const unsigned int nchunks,    // number of virtual chunks
              const unsigned int chunk_id    // chunk index
	    )
{
  size_t base;
  size_t remainder;

  base = nkeys / (size_t) nchunks;
  remainder = nkeys % (size_t) nchunks;

  if ((size_t) chunk_id < remainder){
    return (size_t) chunk_id * (base + 1);
  }

  return remainder * (base + 1) + ((size_t) chunk_id - remainder) * base;
}

/*
  Compute the exclusive end index of a "virtual chunk".
*/
size_t chunk_end ( const size_t       nkeys,      // total number of keys
            const unsigned int nchunks,    // number of virtual chunks
            const unsigned int chunk_id    // chunk index
	        )
{
  return chunk_begin (nkeys, nchunks, chunk_id + 1);
}

/*
  Merge two adjacent sorted runs from data into scratch and copy the result back
  in the caller's pass.
  This is the primitive used both by local chunk sorting
  and by sorting the global sample array.
*/
static void
merge_runs ( sort_key_t *data,       // array containing the input runs
             sort_key_t *scratch,    // temporary array receiving the merged run
             size_t      left,       // first index of the left run
             size_t      middle,     // first index of the right run
             size_t      right       // one-past-last index of the right run 
           )
{
  size_t i;
  size_t j;
  size_t k;

  i = left;
  j = middle;
  k = left;

  while (i < middle && j < right)
    {
      if (data[i] <= data[j])
        scratch[k++] = data[i++];
      else
        scratch[k++] = data[j++];
    }

  while (i < middle)
    scratch[k++] = data[i++];

  while (j < right)
    scratch[k++] = data[j++];
}

/*
  Sort data[begin:end) with a bottom-up merge sort.
  The implementation is straightforward: every pass writes merged runs to scratch and
  copies the pass back to data.
*/
static void
merge_sort_range ( sort_key_t *data,      // array containing the range to sort
                   sort_key_t *scratch,   // temporary array with at least end elements
                   size_t      begin,     // first index of the sorted range
                   size_t      end        // one-past-last index of the sorted range
                 )
{
  size_t n;
  size_t width;
  size_t left;
  size_t middle;
  size_t right;

  if (end <= begin + 1)
    return;

  n = end - begin;

  for (width = 1; width < n; width *= 2)
    {
      for (left = begin; left < end; left += 2 * width)
        {
          middle = left + width;
          if (middle > end)
            middle = end;

          right = left + 2 * width;
          if (right > end)
            right = end;

          merge_runs (data, scratch, left, middle, right);
        }

      memcpy (data + begin, scratch + begin, n * sizeof (sort_key_t));

      if (width > n / 2)
        break;
    }
}

/*
  Sort data[begin:end) with a recursive parallel bottom-up merge sort.
*/
static
void merge_sort_omp_rec ( sort_key_t *data,      // array containing the range to sort
                          sort_key_t *scratch,   // temporary array with at least end elements
                          size_t      begin,     // first index of the sorted range
                          size_t      end,       // one-past-last index of the sorted range
                          size_t      cutoff     // minimum size to make the serial implementation kick-in
                   )
{
  if (end - begin <= 1) return;
  if (end - begin <= cutoff) {
    merge_sort_range (data, scratch, begin, end);
    return;
  }
  size_t mid = begin + (end - begin) / 2;

  #pragma omp task shared(data, scratch)
  merge_sort_omp_rec (data, scratch, begin, mid, cutoff);

  #pragma omp task shared(data, scratch)
  merge_sort_omp_rec (data, scratch, mid, end, cutoff);

  #pragma omp taskwait
  merge_runs (data, scratch, begin, mid, end);
  memcpy (data + begin, scratch + begin, (end - begin) * sizeof (sort_key_t));
}

/*
  Entrypoint for the merge_sort_omp_rec
  Notice: omp single required for the first one
*/
void merge_sort_omp (sort_key_t *data,      // array containing the range to sort
                     sort_key_t *scratch,   // temporary array with at least end elements
                     size_t      begin,     // first index of the sorted range
                     size_t      end        // one-past-last index of the sorted range
                    )
{
  size_t cutoff = 1024;
  #pragma omp parallel
  {
    #pragma omp single
    merge_sort_omp_rec (data, scratch, begin, end, cutoff);
  }
}

/* 
   Extracts a specific digit (8 bits) from a key.
*/
static inline unsigned int get_digit(sort_key_t key, int digit_index) {
    return (unsigned int)((key >> (digit_index * RADIX_BITS)) & MASK);
}

/*
  Serial radix sort helper for a single range [begin, end).
*/
static void radix_sort_range(sort_key_t *data, 
                             sort_key_t *scratch, 
                             size_t begin, 
                             size_t end) 
{
    size_t n = end - begin;
    if (n <= 1) return;

    size_t count[NBUCKETS];
    size_t offset[NBUCKETS];
    
    sort_key_t *src = data + begin;
    sort_key_t *dst = scratch + begin;

    // Number of passes needed for sort_key_t (e.g., 8 passes for uint64_t with 8-bit digits)
    int total_digits = (int)(sizeof(sort_key_t) * 8 / RADIX_BITS);

    for (int dig = 0; dig < total_digits; dig++) {
        memset(count, 0, NBUCKETS * sizeof(size_t));

        for (size_t i = 0; i < n; i++) {
            unsigned int bucket = get_digit(src[i], dig);
            count[bucket]++;
        }

        offset[0] = 0;
        for (int b = 1; b < NBUCKETS; b++) {
            offset[b] = offset[b - 1] + count[b - 1];
        }

        for (size_t i = 0; i < n; i++) {
            unsigned int bucket = get_digit(src[i], dig);
            dst[offset[bucket]++] = src[i];
        }

        // Swap src and dst pointers for the next pass
        sort_key_t *tmp = src;
        src = dst;
        dst = tmp;
    }

    // Copy back if the final sorted result ended up in scratch
    if (src != data + begin) {
        memcpy(data + begin, scratch + begin, n * sizeof(sort_key_t));
    }
}

/*
  Parallel Radix Sort for data[begin:end) using OpenMP.
*/
void radix_sort_omp(sort_key_t *data, 
                    sort_key_t *scratch, 
                    size_t begin, 
                    size_t end
                   ) 
{
    size_t n = end - begin;
    if (n <= 1) return;

    // Fall back to serial sort for small datasets to avoid parallel overhead
    if (n < 1024) {
        radix_sort_range(data, scratch, begin, end);
        return;
    }

    sort_key_t *src = data + begin;
    sort_key_t *dst = scratch + begin;

    int total_digits = (int)(sizeof(sort_key_t) * 8 / RADIX_BITS);

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        int nthreads = omp_get_num_threads();

        // Allocate local thread accumulation arrays on stack
        size_t local_count[NBUCKETS];
        size_t local_offset[NBUCKETS];

        // Shared matrix for thread counts: Gl[thread_id][bucket]
        static size_t **Gl = NULL;

        #pragma omp single
        {
            Gl = (size_t **)malloc(nthreads * sizeof(size_t *));
            for (int t = 0; t < nthreads; t++) {
                // Ensure alignment to 64-byte cache line (assumptuion on the cache-line dimension)
                posix_memalign((void **)&Gl[t], 64, NBUCKETS * sizeof(size_t));
            }
        }
        
        for (int dig = 0; dig < total_digits; dig++) {
            memset(local_count, 0, NBUCKETS * sizeof(size_t));
            // count sort
            #pragma omp for schedule(static)
            for (size_t i = 0; i < n; i++) {
                unsigned int bucket = get_digit(src[i], dig);
                local_count[bucket]++;
            }

            // Copy thread local counts to shared Gl matrix
            for (int b = 0; b < NBUCKETS; b++) {
                Gl[tid][b] = local_count[b];
            }

            #pragma omp barrier // wait for everyone to synch

            // Parallel Prefix Sum (Compute starting write positions per bucket & thread)
            for (int b = 0; b < NBUCKETS; b++) {
                size_t sum = 0;
                // Sum all preceding buckets across all threads
                for (int prev_b = 0; prev_b < b; prev_b++) {
                    for (int t = 0; t < nthreads; t++) {
                        sum += Gl[t][prev_b];
                    }
                }
                // Add preceding threads for the CURRENT bucket
                for (int t = 0; t < tid; t++) {
                    sum += Gl[t][b];
                }
                local_offset[b] = sum;
            }

            // Scatter Step into destination array
            #pragma omp for schedule(static)
            for (size_t i = 0; i < n; i++) {
                unsigned int bucket = get_digit(src[i], dig);
                dst[local_offset[bucket]++] = src[i]; // increment local_offset[bucket] after the access
            }

            // Pointer Swap for Next Pass
            #pragma omp single
            {
                sort_key_t *tmp = src;
                src = dst;
                dst = tmp;
            } // Implicit barrier
        }

        // Copy back if final pass ended up in scratch array
        #pragma omp single
        {
            if (src != data + begin) {
                memcpy(data + begin, scratch + begin, n * sizeof(sort_key_t));
            }

            for (int t = 0; t < nthreads; t++) {
                free(Gl[t]);
            }
            free(Gl);
        }
    }
}

/*
  Sort each virtual rank's local chunk independently.
*/
void sort_virtual_chunks (sort_key_t    *keys,        // key array split into virtual chunks
                          sort_key_t    *scratch,     // temporary array for merge sort
                          size_t         nkeys,       // total number of keys
                          unsigned int   nchunks,     // number of virtual chunks
                          base_sorting sort_algo      // local sorting algorithm
                        ) 
{
  if (sort_algo == MERGE_SORT) {
    for (unsigned int rank = 0; rank < nchunks; rank++) {
      size_t begin = chunk_begin (nkeys, nchunks, rank);
      size_t end = chunk_end (nkeys, nchunks, rank);
      merge_sort_omp (keys, scratch, begin, end);
    }
  }
  if (sort_algo == RADIX_SORT){
    for (unsigned int rank = 0; rank < nchunks; rank++){
      size_t begin = chunk_begin (nkeys, nchunks, rank);
      size_t end = chunk_end (nkeys, nchunks, rank);
      radix_sort_omp (keys, scratch, begin, end);
    }
  }
  // TO IMPLEMENT THE REMAINING OTHERS:
  // otpimized merge sort
  // quick sort
}