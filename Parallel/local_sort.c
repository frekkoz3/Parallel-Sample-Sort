#include "local_sort.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h> // debug
#include <omp.h>

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
   : INSERTION SORT                                         :
   : ------------------------------------------------------ :
*/ 
static 
void insertion_sort_range(  sort_key_t *data,      // array containing the range to sort
                            size_t      begin,     // first index of the sorted range
                            size_t      end        // one-past-last index of the sorted range
)
{
  sort_key_t temp;
  int j;
  for (int i = (int)(begin + 1); i < (int)end; i++){
    temp = data[i];
    j = i - 1;
    while(j >= (int)begin && data[j] > temp){
      data[j+1] = data[j];
      j--;
    }
    data[j+1] = temp;
  }
}

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
  // branchless version
  while (i < middle && j < right) {
    int cmp = data[i] <= data[j];
    scratch[k++] = cmp ? data[i] : data[j];
    i += cmp;
    j += !cmp;
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

        
        // private per-thread arrays
        size_t local_count[n_buckets];
        size_t local_offset[n_buckets];

        
        
        // allocate shared structures once
        #pragma omp single
        {
            Gl = (size_t **)malloc(nthreads * sizeof(size_t *));

            for (int t = 0; t < nthreads; t++) {
                posix_memalign((void **)&Gl[t],
                               64,
                               n_buckets * sizeof(size_t));
            }
            // alignment for cache friendly behavior
            posix_memalign((void **)&bucket_base,
                           64,
                           n_buckets * sizeof(size_t));
        }

        #pragma omp barrier
        
        // count sort
        for (int dig = 0; dig < total_digits; dig++) {

            // local histogram
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

            // local histogram to global
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

            // compute this thread's starting offset
            //inside every bucket.

            for (int b = 0; b < n_buckets; b++) {

                size_t offset = bucket_base[b];

                for (int t = 0; t < tid; t++) {
                    offset += Gl[t][b];
                }

                local_offset[b] = offset;
            }

            // scatter

            #pragma omp for schedule(static)
            for (size_t i = 0; i < n; i++) {

                unsigned int bucket =
                    get_digit(src[i],
                              dig,
                              digit_bits,
                              mask);

                dst[local_offset[bucket]++] = src[i];
            }

            // ping pong buffer

            #pragma omp single
            {
                sort_key_t *tmp = src;
                src = dst;
                dst = tmp;
            }

            // Implicit Barrier Here !!
        }

        
        
        // This is done to ensure the results are in src
        // (Could not be the case for the pingpong style)
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
   : ------------------------------------------------------ :
   : QUICK SORT                                             :
   : ------------------------------------------------------ :
*/ 

/*
  MEDIAN3
  return the median between 3 elements
*/
static sort_key_t median3(sort_key_t a,
                          sort_key_t b,
                          sort_key_t c)
{
    if (a < b) {
        if (b < c) return b;
        return a < c ? c : a;
    } else {
        if (a < c) return a;
        return b < c ? c : b;
    }
}

/*
 Tukey's ninther pivoting strategy: 
 takes 3 groups of 3 elements (equidistant) and compute the median of each 
 of the 3 group and then the median of the median
*/
static sort_key_t ninther(sort_key_t *data,
                          size_t begin,
                          size_t end)
{
    size_t n = end - begin;

    size_t step = n / 8;

    sort_key_t x0 = data[begin];
    sort_key_t x1 = data[begin + step];
    sort_key_t x2 = data[begin + 2 * step];

    sort_key_t x3 = data[begin + 3 * step];
    sort_key_t x4 = data[begin + 4 * step];
    sort_key_t x5 = data[begin + 5 * step];

    sort_key_t x6 = data[begin + 6 * step];
    sort_key_t x7 = data[begin + 7 * step];
    sort_key_t x8 = data[end - 1];

    sort_key_t m0 = median3(x0, x1, x2);
    sort_key_t m1 = median3(x3, x4, x5);
    sort_key_t m2 = median3(x6, x7, x8);

    return median3(m0, m1, m2);
}

/*
  ITM (in the middle) Pivot
*/
static sort_key_t itm_pivot(sort_key_t *data,
                          size_t begin,
                          size_t end
                        )
{
  size_t mid = (begin + end)/2;
  return data[mid];
}

/*
  MITM (median in the middle) Pivot.
  Takes the 3 values at the middle of the array 
  and returns the median.
  If the data are uniformly distributed,
  the 3 median of the 3 values is statistically a good estimate 
  of the real median. 
  Why the 3 central values?
  Because they are fast to compute and live pretty near.
*/
static sort_key_t mitm_pivot(sort_key_t *data, 
                              size_t begin,
                              size_t end)
{
  if (end - begin < 3){
    return data[(end+begin)/2];
  }
  size_t mid = (begin + end)/2;
  size_t l_mid = mid-1;
  size_t u_mid = mid+1;
  return median3(data[l_mid], data[mid], data[u_mid]);
}
/*
  3-WAY PARTITION
  this is really usefull for array with duplicates
 
  Partition arr[begin:end) into:

    [begin, lt)   < pivot
    [lt, gt)      == pivot
    [gt, end)     > pivot
 
  lt and gt are returned through the pointer arguments.
 */
static void partition_3way(sort_key_t *arr,
                           size_t begin,
                           size_t end,
                           sort_key_t pivot,
                           size_t *lt,
                           size_t *gt)
{
    size_t low = begin;
    size_t i = begin;
    size_t high = end;

    while (i < high) {

        if (arr[i] < pivot) {

            swap(&arr[low], &arr[i]);

            ++low;
            ++i;

        } else if (arr[i] > pivot) {

            high--;

            swap(&arr[i], &arr[high]);

        } else {

            i++;
        }
    }

    *lt = low;
    *gt = high;
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
    if (end - begin <= INSERTION_SORT_CUTOFF){
      insertion_sort_range(data, begin, end);
      return;
    }

    int use_mitm = 1;
    int use_itm = 0;
    sort_key_t pivot;

    if (use_mitm == 1){
      pivot = mitm_pivot(data, begin, end);
    }
    else if (use_itm == 1)
    {
      pivot = itm_pivot(data, begin, end);
    }
    else 
    {
      pivot = ninther(data, begin, end);
    }
    
    size_t lt;
    size_t gt;

    partition_3way(data, begin, end, pivot, &lt, &gt);

    quick_sort_range(data, begin, lt);
    quick_sort_range(data, gt, end);
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

    int use_mitm = 1;
    int use_itm = 0;
    sort_key_t pivot;

    if (use_mitm == 1){
      pivot = mitm_pivot(data, begin, end);
    }
    else if (use_itm == 1)
    {
      pivot = itm_pivot(data, begin, end);
    }
    else 
    {
      pivot = ninther(data, begin, end);
    }

    size_t lt;
    size_t gt;

    partition_3way(data, begin, end, pivot, &lt, &gt);

    #pragma omp task
    quick_sort_omp_rec(data, begin, lt);

    quick_sort_omp_rec(data, gt, end);

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
  Notice : this is "useful" only if you do not use any MPI and so
  you "simulate it" with virtual chunks
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

/*
  Sort each rank (this is just a router)
*/
void sort_rank(sort_key_t *keys,
               sort_key_t *scratch,
               size_t nkeys,
               options_t *options)
{
    base_sorting sort_algo =
        (base_sorting)options->sorting;

    if (sort_algo == MERGE_SORT) {
        merge_sort_omp(keys, scratch, 0, nkeys);
    }
    else if (sort_algo == RADIX_SORT) {
        radix_sort_omp(keys, scratch, 0, nkeys, (int)options->radix_bits);
    }
    else if (sort_algo == QUICK_SORT) {
        quick_sort_omp(keys, 0, nkeys);
    }
}