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
  This implementation could be improved by using the "pingpong technique" (this is done in the radix sort)

  The idea is that one pass the input are in data and the output in scratch
  while the following pass the input are in scratch and the output in data

  This prevent to copy each pass the entire range back from scratch to data

  This technique almost halves the memory movement
*/
static
void merge_sort_omp_rec ( sort_key_t *data,      // array containing the range to sort
                          sort_key_t *scratch,   // temporary array with at least end elements
                          size_t      begin,     // first index of the sorted range
                          size_t      end       // one-past-last index of the sorted range
                   )
{
  if (end - begin <= 1) return;
  if (end - begin <= DEFAULT_SERIAL_CUTOFF) {
    merge_sort_range (data, scratch, begin, end);
    return;
  }
  size_t mid = begin + (end - begin) / 2;

  #pragma omp task shared(data, scratch)
  merge_sort_omp_rec (data, scratch, begin, mid);

  #pragma omp task shared(data, scratch)
  merge_sort_omp_rec (data, scratch, mid, end);

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
  #pragma omp parallel
  {
    #pragma omp single // only one thread must init the recursion !!!
    merge_sort_omp_rec (data, scratch, begin, end);
  }
}

/* 
   : ------------------------------------------------------ :
   : RADIX SORT                                             :
   : ------------------------------------------------------ :
*/ 

/* 
   Extracts a specific digit (8 bits) from a key.
*/
static inline unsigned int get_digit(sort_key_t key, int digit_index, int digit_bits, int mask) {
    return (unsigned int)((key >> (digit_index * digit_bits)) & mask);
}

/*
  Serial radix sort helper for a single range [begin, end).
*/
static void radix_sort_range(sort_key_t *data, 
                             sort_key_t *scratch, 
                             size_t begin, 
                             size_t end,
                             int digit_bits
                             )
{
    int n_buckets = (1 << digit_bits);
    int mask = n_buckets - 1;

    size_t n = end - begin;
    if (n <= 1) return;

    size_t count[n_buckets];
    size_t offset[n_buckets];
    
    sort_key_t *src = data + begin;
    sort_key_t *dst = scratch + begin;

    // in this moment this work only when digit_bits is an exact divisor of N_BITS  !!!
    int total_digits = (int)(sizeof(sort_key_t) * 8 / digit_bits);

    for (int dig = 0; dig < total_digits; dig++) {
        memset(count, 0, n_buckets * sizeof(size_t));

        for (size_t i = 0; i < n; i++) {
            unsigned int bucket = get_digit(src[i], dig, digit_bits, mask);
            count[bucket]++;
        }

        offset[0] = 0;
        for (int b = 1; b < n_buckets; b++) {
            offset[b] = offset[b - 1] + count[b - 1];
        }

        for (size_t i = 0; i < n; i++) {
            unsigned int bucket = get_digit(src[i], dig, digit_bits, mask);
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
                    size_t end,
                    int digit_bits)
{
    int n_buckets = (1 << digit_bits);
    int mask = n_buckets - 1;

    size_t n = end - begin;

    if (n <= 1)
        return;

    if (n < DEFAULT_SERIAL_CUTOFF) {
        radix_sort_range(data, scratch, begin, end, digit_bits);
        return;
    }

    sort_key_t *src = data + begin;
    sort_key_t *dst = scratch + begin;

    int total_digits =
        (int)(sizeof(sort_key_t) * 8 / digit_bits);

    /*
     * Gl[t][b] = number of elements owned by thread t
     *            that belong to bucket b.
     */
    size_t **Gl = NULL;

    /*
     * bucket_base[b] = global starting position of bucket b.
     */
    size_t *bucket_base = NULL;

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        int nthreads = omp_get_num_threads();

        /*
         * Private per-thread arrays.
         */
        size_t local_count[n_buckets];
        size_t local_offset[n_buckets];

        /*
         * Allocate shared structures once.
         */
        #pragma omp single
        {
            Gl = (size_t **)malloc(nthreads * sizeof(size_t *));

            for (int t = 0; t < nthreads; t++) {
                posix_memalign((void **)&Gl[t],
                               64,
                               n_buckets * sizeof(size_t));
            }

            posix_memalign((void **)&bucket_base,
                           64,
                           n_buckets * sizeof(size_t));
        }

        #pragma omp barrier
        
        /*
         * Count Sort
        */
        for (int dig = 0; dig < total_digits; dig++) {

            /*
             * Local histograms
             */

            memset(local_count,
                   0,
                   n_buckets * sizeof(size_t));

            #pragma omp for schedule(static)
            for (size_t i = 0; i < n; i++) {

                unsigned int bucket =
                    get_digit(src[i],
                              dig,
                              digit_bits,
                              mask);

                local_count[bucket]++;
            }

            /*
             * Local histograms to Global
             */

            for (int b = 0; b < n_buckets; b++) {
                Gl[tid][b] = local_count[b];
            }

            #pragma omp barrier

            /*
             * Compute global bucket starting positions
             *
             * bucket_base[b] =
             *     number of elements belonging to
             *     buckets smaller than b.
             */

            #pragma omp single
            {
                size_t sum = 0;

                for (int b = 0; b < n_buckets; b++) {

                    bucket_base[b] = sum;

                    for (int t = 0; t < nthreads; t++) {
                        sum += Gl[t][b];
                    }
                }
            }

            #pragma omp barrier

            /*
             * Compute this thread's starting offset
             * inside every bucket.
             */

            for (int b = 0; b < n_buckets; b++) {

                size_t offset = bucket_base[b];

                for (int t = 0; t < tid; t++) {
                    offset += Gl[t][b];
                }

                local_offset[b] = offset;
            }

            /*
             * Scatter
             */

            #pragma omp for schedule(static)
            for (size_t i = 0; i < n; i++) {

                unsigned int bucket =
                    get_digit(src[i],
                              dig,
                              digit_bits,
                              mask);

                dst[local_offset[bucket]++] = src[i];
            }

            /*
             * Ping-pong buffers
             */

            #pragma omp single
            {
                sort_key_t *tmp = src;
                src = dst;
                dst = tmp;
            }

            // Implicit Barrier Here !!
        }

        
        /*
         * This is done to ensure the results are in src
         * (Could not be the case for the pingpong style)
        */
        #pragma omp single
        {
            if (src != data + begin) {
                memcpy(data + begin,
                       src,
                       n * sizeof(sort_key_t));
            }

            for (int t = 0; t < nthreads; t++) {
                free(Gl[t]);
            }

            free(Gl);
            free(bucket_base);
        }
    }
}

/*
  Helper to swap two elements
*/
static void swap(sort_key_t *a,
                 sort_key_t *b
                )
{
    sort_key_t temp = *a;
    *a = *b;
    *b = temp;
}

/* 
   : ------------------------------------------------------ :
   : QUICK SORT                                             :
   : ------------------------------------------------------ :
*/ 

/*
  Partition data[begin:end).
  Returns the final position of the pivot.
*/
static size_t partition(sort_key_t *arr,
                        size_t begin, 
                        size_t end
                      )
{
    sort_key_t pivot = arr[begin];

    size_t i = begin + 1;
    size_t j = end - 1;

    while (1) {

        while (i <= j && arr[i] <= pivot) {
            i++;
        }

        while (i <= j && arr[j] > pivot) {
            j--;
        }

        if (i >= j) {
            break;
        }

        swap(&arr[i], &arr[j]);
        i++;
        j--;
    }

    swap(&arr[begin], &arr[j]);

    return j;
}

/*
  Serial quick sort for data[begin:end)
*/
static void quick_sort_range(sort_key_t *data,
                             size_t begin,
                             size_t end)
{
    if (end - begin <= 1) {
        return;
    }
    // could be improved with a MoM strategy
    size_t mid = begin + (end - begin) / 2;
    swap(&data[begin], &data[mid]);
    size_t pi = partition(data, begin, end);

    quick_sort_range(data, begin, pi);
    quick_sort_range(data, pi + 1, end);
}

/*
  Parallel quick sort helper for data[begin:end)
*/
static void quick_sort_omp_rec(sort_key_t *data,
                               size_t begin,
                               size_t end)
{
    if (end - begin <= 1) {
        return;
    }

    if (end - begin < DEFAULT_SERIAL_CUTOFF) {
        quick_sort_range(data, begin, end);
        return;
    }
    // could be improved with a MoM strategy
    size_t mid = begin + (end - begin) / 2;
    swap(&data[begin], &data[mid]);
    size_t pi = partition(data, begin, end);

    #pragma omp task
    quick_sort_omp_rec(data, begin, pi);

    #pragma omp task // could we remove this since the father is just idle?
    quick_sort_omp_rec(data, pi + 1, end);

    #pragma omp taskwait
}

/*
  Entry point for parallel quick sort
*/
void quick_sort_omp(sort_key_t *data,
                    size_t begin,
                    size_t end)
{
    #pragma omp parallel
    {
        #pragma omp single
        quick_sort_omp_rec(data, begin, end);
        
    }
}

/*
  Sort each virtual rank's local chunk independently.
*/
void sort_virtual_chunks (sort_key_t    *keys,        // key array split into virtual chunks
                          sort_key_t    *scratch,     // temporary array for merge sort
                          size_t         nkeys,       // total number of keys
                          options_t     *options      // various options
                        ) 
{
  unsigned int nchunks = (unsigned int)options->nbuckets;
  base_sorting sort_algo = (base_sorting)options->sorting;
  if (sort_algo == MERGE_SORT) {
    for (unsigned int rank = 0; rank < nchunks; rank++) {
      size_t begin = chunk_begin (nkeys, nchunks, rank);
      size_t end = chunk_end (nkeys, nchunks, rank);
      merge_sort_omp (keys, scratch, begin, end);
    }
  }
  if (sort_algo == RADIX_SORT){
    for (unsigned int rank = 0; rank < nchunks; rank++){
      int digit_bits = (int)options->radix_bits;
      size_t begin = chunk_begin (nkeys, nchunks, rank);
      size_t end = chunk_end (nkeys, nchunks, rank);
      radix_sort_omp (keys, scratch, begin, end, digit_bits);
    }
  }
  if (sort_algo == QUICK_SORT){
    for (unsigned int rank = 0; rank < nchunks; rank++){
      size_t begin = chunk_begin (nkeys, nchunks, rank);
      size_t end = chunk_end (nkeys, nchunks, rank);
      quick_sort_omp (keys, begin, end);
    }
  }
}