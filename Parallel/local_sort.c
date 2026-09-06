#include "local_sort.h"
#include <string.h>
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
  // TO IMPLEMENT THE REMAINING OTHERS:
  // otpimized merge sort
  // quick sort
  // heap sort
}