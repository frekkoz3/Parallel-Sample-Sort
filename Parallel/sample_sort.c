#include "sample_sort.h"
#include "local_sort.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
  Select regular samples from every sorted virtual chunk.
  Regular sampling is later implemented before the MPI_ gatehring of samples; this
  serial function stores the gathered samples directly in one array.
*/
static void
select_regular_samples ( sort_key_t    *keys,                // locally sorted chunks
                         size_t         nkeys,               // total number of keys
                         unsigned int   nchunks,             // number of virtual chunks
                         size_t         samples_per_chunk,   // samples selected from each chunk
                         sort_key_t    *samples              // gathered sample array
		       )
{
  unsigned int rank;
  size_t       begin;
  size_t       end;
  size_t       chunk_size;
  size_t       s;
  size_t       local_index;
  size_t       sample_id;

  sample_id = 0;

  for (rank = 0; rank < nchunks; rank++)
    {
      begin = chunk_begin (nkeys, nchunks, rank);
      end = chunk_end (nkeys, nchunks, rank);
      chunk_size = end - begin;

      for (s = 1; s <= samples_per_chunk; s++)
        {
          local_index = (s * chunk_size) / (samples_per_chunk + 1);
          if (local_index >= chunk_size)
            local_index = chunk_size - 1;

          samples[sample_id++] = keys[begin + local_index];
        }
    }
}

/*
  Choose global pivots from the sorted sample array.
  With nbuckets = P, the pivots split the key space into P buckets.
  The pivoting is implemented simply.
  Experiment with stronger oversampling or different pivot positioning
*/
static void
choose_global_pivots ( sort_key_t    *samples,             // sorted gathered samples
                       size_t         samples_per_chunk,   // samples contributed by each virtual rank
                       unsigned int   nbuckets,            // number of output buckets
                       sort_key_t    *pivots               // output array of nbuckets - 1 pivots
		     )
{
  unsigned int bucket;
  size_t       nsamples;
  size_t       pivot_index;

  if (nbuckets <= 1)
    return;

  nsamples = (size_t) nbuckets * samples_per_chunk;

  for (bucket = 1; bucket < nbuckets; bucket++)
    {
      pivot_index = (size_t) bucket * samples_per_chunk;

      if (pivot_index >= nsamples)
        pivot_index = nsamples - 1;

      pivots[bucket - 1] = samples[pivot_index];
    }
}
/*
  Find the first position in data[begin:end) with value greater than key.
  Using upper_bound means that keys equal to a pivot stay in the lower bucket.
  If there are many data with the same value, this could create imbalance.
*/
static size_t
upper_bound_key ( sort_key_t   *data,    // sorted array
                  size_t        begin,   // first search index
                  size_t        end,     // one-past-last search index
                  sort_key_t    key      // pivot key
		)
{
  size_t left;
  size_t right;
  size_t middle;

  left = begin;
  right = end;

  while (left < right)
    {
      middle = left + (right - left) / 2;

      if (data[middle] <= key)
        left = middle + 1;
      else
        right = middle;
    }

  return left;
}

/*
  Partition every sorted virtual chunk into nbuckets sorted subranges.
  In MPI this becomes the send counter and displacement arrays used by (possibly)
  MPI_Alltoall and MPI_Alltoallv.
*/
static void
build_bucket_bounds ( sort_key_t    *keys,       // locally sorted chunks
                      size_t         nkeys,      // total number of keys
                      unsigned int   nchunks,    // number of virtual chunks
                      sort_key_t    *pivots,     // global pivots
                      size_t        *bounds      // output matrix nchunks x (nchunks + 1)
		    )
{
  unsigned int rank;
  unsigned int bucket;
  size_t       begin;
  size_t       end;
  size_t       cursor;
  size_t       pos;
  size_t       row;

  for (rank = 0; rank < nchunks; rank++)
    {
      begin = chunk_begin (nkeys, nchunks, rank);
      end = chunk_end (nkeys, nchunks, rank);
      cursor = begin;
      row = (size_t) rank * ((size_t) nchunks + 1);

      bounds[row] = begin;

      for (bucket = 0; bucket + 1 < nchunks; bucket++)
        {
          pos = upper_bound_key (keys, cursor, end, pivots[bucket]);
          bounds[row + (size_t) bucket + 1] = pos;
          cursor = pos;
        }

      bounds[row + (size_t) nchunks] = end;
    }
}


/*
  Compute the output offset of every destination bucket.
  In the parallel code, these sizes are the receive counters after MPI_All...
  In this serial code,
  they tell where each final bucket starts in the globally sorted output.
*/
static void
compute_bucket_starts ( size_t        *bounds,          // source/destination boundaries
                        unsigned int   nchunks,         // number of virtual chunks and buckets
                        size_t        *bucket_starts    // output prefix sum, length nchunks + 1
		      )
{
  unsigned int      destination;
  unsigned int      source;
  size_t            total;
  size_t            row;
  size_t            count;

  bucket_starts[0] = 0;

  for (destination = 0; destination < nchunks; destination++)
    {
      total = 0;

      for (source = 0; source < nchunks; source++)
        {
          row = (size_t) source * ((size_t) nchunks + 1);
          count = bounds[row + (size_t) destination + 1] - bounds[row + (size_t) destination];
          total += count;
        }

      bucket_starts[(size_t) destination + 1] = bucket_starts[destination] + total;
    }
}

/*
  Merge all sorted incoming streams for one destination bucket.
  The code scans all current stream heads to find the next key.
  This is clear but maybe not optimal.
  This is the place to possible compare different merging strategies
*/
static void
basic_iterative_k_way_merge_buckets ( sort_key_t    *keys,          // source sorted chunks
                                      size_t        *bounds,        // source/destination boundaries
                                      unsigned int   nchunks,       // number of incoming streams
                                      unsigned int   destination,   // destination bucket to merge
                                      sort_key_t    *output,        // full output array
                                      size_t         out_begin     // first output index for this bucket
			 )
{
  size_t      *current;
  size_t      *end;
  unsigned int source;
  unsigned int best_source;
  int          have_best;
  sort_key_t   best_key;
  size_t       row;
  size_t       out;
  size_t       out_end;

  current = malloc_array ((size_t) nchunks, sizeof (size_t));
  end = malloc_array ((size_t) nchunks, sizeof (size_t));

  out = out_begin;
  out_end = out_begin;

  for (source = 0; source < nchunks; source++)
    {
      row = (size_t) source * ((size_t) nchunks + 1);
      current[source] = bounds[row + (size_t) destination];
      end[source] = bounds[row + (size_t) destination + 1];
      out_end += end[source] - current[source];
    }

  while (out < out_end)
    {
      have_best = 0;
      best_source = 0;
      best_key = 0;

      for (source = 0; source < nchunks; source++)
        {
          if (current[source] < end[source])
            {
              if (!have_best || keys[current[source]] < best_key)
                {
                  have_best = 1;
                  best_source = source;
                  best_key = keys[current[source]];
                }
            }
        }

      // have_best must be true while out < out_end.  If it is not, the bucket
      // boundary arithmetic above is inconsistent.
      if (!have_best)
        {
          fprintf (stderr, "Internal error during k-way merge\n");
          free (current);
          free (end);
          exit (EXIT_FAILURE);
        }

      output[out++] = best_key;
      current[best_source] += 1;
    }

  free (current);
  free (end);
}

/*
  Binary iterative K-way merge.
    Merge the incoming sorted streams in a binary/tree fashion:
    round 0:  (0,1), (2,3), (4,5), ...
    round 1:  (0..1,2..3), (4..5,6..7), ...
    round 2:  ...

  The implementation uses a ping-pong strategy based on
  the bucket and a temporary buffer between rounds
*/
static void
binary_iterative_k_way_merge_buckets (  sort_key_t    *keys,          // source sorted chunks
                                        size_t        *bounds,        // source/destination boundaries
                                        unsigned int   nchunks,       // number of incoming streams
                                        unsigned int   destination,   // destination bucket to merge
                                        sort_key_t    *output,        // full output array
                                        size_t         out_begin      // first output index for this bucket
                                      )
{
  size_t *begin;
  size_t *end;
  size_t *new_begin;
  size_t *new_end;

  sort_key_t *src;
  sort_key_t *dst;
  sort_key_t *tmp;

  unsigned int source;
  unsigned int nruns;
  unsigned int new_nruns;
  unsigned int i;

  size_t total;
  size_t pos;

  if (nchunks == 0) // just in case
    return;

  /*
    Find the total size of the destination bucket.
  */
  total = 0;

  for (source = 0; source < nchunks; source++)
    {
      size_t row;

      row = (size_t) source * ((size_t) nchunks + 1);

      total += bounds[row + (size_t) destination + 1]
             - bounds[row + (size_t) destination];
    }

  if (total == 0)
    return;

  /*
    Temporary storage for the ping-pong merge.
  */
  tmp = malloc_array (total, sizeof (sort_key_t));

  begin = malloc_array ((size_t) nchunks, sizeof (size_t));
  end = malloc_array ((size_t) nchunks, sizeof (size_t));
  new_begin = malloc_array ((size_t) nchunks, sizeof (size_t));
  new_end = malloc_array ((size_t) nchunks, sizeof (size_t));

  /*
    First copy all source runs into one contiguous temporary array.
    After this point all indexes are relative to this bucket.
  */
  pos = 0;

  for (source = 0; source < nchunks; source++)
    {
      size_t row;
      size_t source_begin;
      size_t source_end;
      size_t j;

      row = (size_t) source * ((size_t) nchunks + 1);

      source_begin = bounds[row + (size_t) destination];
      source_end = bounds[row + (size_t) destination + 1];

      begin[source] = pos;
      end[source] = pos + source_end - source_begin;

      for (j = source_begin; j < source_end; j++)
        tmp[pos++] = keys[j];
    }

  /*
    At the beginning, all runs are in tmp.
  */
  src = tmp;
  dst = output + out_begin;

  nruns = nchunks;

  while (nruns > 1)
    {
      new_nruns = 0;

      for (i = 0; i < nruns; i += 2)
        {
          size_t left;
          size_t left_end;
          size_t right;
          size_t right_end;
          size_t out;

          /*
            First run of the pair.
          */
          left = begin[i];
          left_end = end[i];

          /*
            Odd run at the end: just copy it.
          */
          if (i + 1 >= nruns)
            {
              out = (new_nruns == 0)
                    ? 0
                    : new_end[new_nruns - 1];

              new_begin[new_nruns] = out;

              while (left < left_end)
                dst[out++] = src[left++];

              new_end[new_nruns] = out;
              new_nruns++;

              continue;
            }

          /*
            Second run of the pair.
          */
          right = begin[i + 1];
          right_end = end[i + 1];

          out = (new_nruns == 0)
                ? 0
                : new_end[new_nruns - 1];

          new_begin[new_nruns] = out;

          /*
            Two-way merge.
            
            On equality choose the left run, matching the behavior
            of basic_iterative_k_way_merge_buckets(), which scans
            sources from low to high.
          */
          while (left < left_end && right < right_end)
            {
              if (!(src[right] < src[left]))
                dst[out++] = src[left++];
              else
                dst[out++] = src[right++];
            }

          while (left < left_end)
            dst[out++] = src[left++];

          while (right < right_end)
            dst[out++] = src[right++];

          new_end[new_nruns] = out;
          new_nruns++;
        }

      /*
        Swap run-boundary arrays.
      */
      {
        size_t *swap;

        swap = begin;
        begin = new_begin;
        new_begin = swap;

        swap = end;
        end = new_end;
        new_end = swap;
      }

      nruns = new_nruns;

      /*
        Swap source and destination buffers.
      */
      {
        sort_key_t *swap;

        swap = src;
        src = dst;
        dst = swap;
      }
    }

  /*
    If the final result ended up in tmp, copy it to output.
    If it already ended up in output, nothing is needed.
  */
  if (src != output + out_begin)
    {
      size_t j;

      for (j = 0; j < total; j++)
        output[out_begin + j] = src[j];
    }

  free (begin);
  free (end);
  free (new_begin);
  free (new_end);
  free (tmp);
}

/*
  Heap direct K-way merge.
*/
static void
heap_direct_k_way_merge_buckets ( sort_key_t    *keys,          // source sorted chunks
                                  size_t        *bounds,        // source/destination boundaries
                                  unsigned int   nchunks,       // number of incoming streams
                                  unsigned int   destination,   // destination bucket to merge
                                  sort_key_t    *output,        // full output array
                                  size_t         out_begin     // first output index for this bucket
                                )
{
    size_t      *current;
  size_t      *end;
  unsigned int source;
  unsigned int best_source;
  int          have_best;
  sort_key_t   best_key;
  size_t       row;
  size_t       out;
  size_t       out_end;

  current = malloc_array ((size_t) nchunks, sizeof (size_t));
  end = malloc_array ((size_t) nchunks, sizeof (size_t));

  out = out_begin;
  out_end = out_begin;

  for (source = 0; source < nchunks; source++)
    {
      row = (size_t) source * ((size_t) nchunks + 1);
      current[source] = bounds[row + (size_t) destination];
      end[source] = bounds[row + (size_t) destination + 1];
      out_end += end[source] - current[source];
    }

  while (out < out_end)
    {
      have_best = 0;
      best_source = 0;
      best_key = 0;

      for (source = 0; source < nchunks; source++)
        {
          if (current[source] < end[source])
            {
              if (!have_best || keys[current[source]] < best_key)
                {
                  have_best = 1;
                  best_source = source;
                  best_key = keys[current[source]];
                }
            }
        }

      // have_best must be true while out < out_end.  If it is not, the bucket
      // boundary arithmetic above is inconsistent.
      if (!have_best)
        {
          fprintf (stderr, "Internal error during k-way merge\n");
          free (current);
          free (end);
          exit (EXIT_FAILURE);
        }

      output[out++] = best_key;
      current[best_source] += 1;
    }

  free (current);
  free (end);
}

/*
  Tournament tree direct K-way merge.
*/
static void
tournament_tree_direct_k_way_merge_buckets (  sort_key_t    *keys,          // source sorted chunks
                                              size_t        *bounds,        // source/destination boundaries
                                              unsigned int   nchunks,       // number of incoming streams
                                              unsigned int   destination,   // destination bucket to merge
                                              sort_key_t    *output,        // full output array
                                              size_t         out_begin     // first output index for this bucket
			                                      )
{
  size_t      *current;
  size_t      *end;
  unsigned int source;
  unsigned int best_source;
  int          have_best;
  sort_key_t   best_key;
  size_t       row;
  size_t       out;
  size_t       out_end;

  current = malloc_array ((size_t) nchunks, sizeof (size_t));
  end = malloc_array ((size_t) nchunks, sizeof (size_t));

  out = out_begin;
  out_end = out_begin;

  for (source = 0; source < nchunks; source++)
    {
      row = (size_t) source * ((size_t) nchunks + 1);
      current[source] = bounds[row + (size_t) destination];
      end[source] = bounds[row + (size_t) destination + 1];
      out_end += end[source] - current[source];
    }

  while (out < out_end)
    {
      have_best = 0;
      best_source = 0;
      best_key = 0;

      for (source = 0; source < nchunks; source++)
        {
          if (current[source] < end[source])
            {
              if (!have_best || keys[current[source]] < best_key)
                {
                  have_best = 1;
                  best_source = source;
                  best_key = keys[current[source]];
                }
            }
        }

      // have_best must be true while out < out_end.  If it is not, the bucket
      // boundary arithmetic above is inconsistent.
      if (!have_best)
        {
          fprintf (stderr, "Internal error during k-way merge\n");
          free (current);
          free (end);
          exit (EXIT_FAILURE);
        }

      output[out++] = best_key;
      current[best_source] += 1;
    }

  free (current);
  free (end);
}

/*
  Merge every destination bucket in increasing pivot order.
  Concatenating these merged buckets gives the final globally sorted array because all keys in
  bucket b are less than or equal to all keys in bucket b + 1 by construction.
*/
static void
merge_all_buckets_omp ( sort_key_t    *keys,                // locally sorted source chunks
                    size_t            *bounds,              // source/destination boundaries
                    size_t            *bucket_starts,       // output prefix sum by destination bucket
                    unsigned int       nchunks,             // number of virtual chunks and buckets
                    sort_key_t        *output,              // globally sorted output array
                    merging_strategy  merging_strat         // k-way merging strategy
		  )
{
  unsigned int destination;
  #pragma omp parallel for schedule(static)
  for (destination = 0; destination < nchunks; destination++){
    if(merging_strat == BINARY_ITERATIVE_KWM){
      binary_iterative_k_way_merge_buckets (keys, bounds, nchunks, destination, output,
                              bucket_starts[destination]);
    }
    if(merging_strat == BASIC_ITERATIVE_KWM){
      basic_iterative_k_way_merge_buckets (keys, bounds, nchunks, destination, output,
                              bucket_starts[destination]);
    }
    if(merging_strat == HEAP_DIRECT_KWM){
      heap_direct_k_way_merge_buckets (keys, bounds, nchunks, destination, output,
                              bucket_starts[destination]);
    }
    if(merging_strat == TORUNAMENT_TREE_DIRECT_KWM){
      tournament_tree_direct_k_way_merge_buckets (keys, bounds, nchunks, destination, output,
                              bucket_starts[destination]);
    }
  }
}

/*
  Run the sample-sort algorithm.
  The input array is modified during the local sort phase;
  the final globally sorted sequence is written to output.
*/
void sample_sort (  sort_key_t    *keys,          // input keys, modified by local sorts
                    sort_key_t    *output,        // globally sorted output keys
                    size_t         nkeys,         // number of keys
                    options_t     *options,       // runtime options
                    timing_t      *timing         // phase timings to fill
      )
{
  sort_key_t *scratch;
  sort_key_t *samples;
  sort_key_t *pivots;
  size_t     *bounds;
  size_t     *bucket_starts;
  size_t      samples_per_chunk;
  size_t      nsamples;
  double      t0;
  double      t1;

  scratch = malloc_array (nkeys, sizeof (sort_key_t));

  // ··············································
  // sort your local chunk

  t0 = wall_seconds ();
  sort_virtual_chunks (keys, scratch, nkeys, options);
  t1 = wall_seconds ();
  timing->local_sort = t1 - t0;

  if (options->nbuckets == 1) {
    memcpy (output, keys, nkeys * sizeof (sort_key_t));
    free (scratch);
    timing->sampling = timing->partitioning = timing->merging = 0.0;
    return;
  }

  // ··············································
  // sample

  samples_per_chunk = options->oversample * (size_t) (options->nbuckets - 1);
  nsamples          = samples_per_chunk * (size_t) options->nbuckets;

  samples        = malloc_array (nsamples, sizeof (sort_key_t));
  pivots         = malloc_array ((size_t) options->nbuckets - 1, sizeof (sort_key_t));
  bounds         = malloc_array ((size_t) options->nbuckets * ((size_t) options->nbuckets + 1), sizeof (size_t));
  bucket_starts  = malloc_array ((size_t) options->nbuckets + 1, sizeof (size_t));

  t0 = wall_seconds ();
  select_regular_samples (keys, nkeys, options->nbuckets, samples_per_chunk, samples);
  
  base_sorting sort_algo = (base_sorting)options->sorting;
  if (sort_algo == MERGE_SORT) {
    sort_key_t *sample_scratch = malloc_array (nsamples, sizeof (sort_key_t));
    merge_sort_omp (samples, sample_scratch, 0, nsamples);
    free (sample_scratch);
  }
  if (sort_algo == RADIX_SORT){
    sort_key_t *sample_scratch = malloc_array (nsamples, sizeof (sort_key_t));
    int digit_bits = (int)options->radix_bits;
    radix_sort_omp (samples, sample_scratch, 0, nsamples, digit_bits);
    free (sample_scratch);
  }
  if (sort_algo == QUICK_SORT){
    quick_sort_omp (samples, 0, nsamples);
  }
  
  choose_global_pivots (samples, samples_per_chunk, options->nbuckets, pivots);
  t1 = wall_seconds ();
  timing->sampling = t1 - t0;

  // ··············································
  // have a global view

  t0 = wall_seconds ();
  build_bucket_bounds (keys, nkeys, options->nbuckets, pivots, bounds);
  compute_bucket_starts (bounds, options->nbuckets, bucket_starts);
  t1 = wall_seconds ();
  timing->partitioning = t1 - t0;

  
  // ··············································
  // merge (here of course we lack exchanging data.. )

  t0 = wall_seconds ();
  merge_all_buckets_omp (keys, bounds, bucket_starts, options->nbuckets, output, options->merging);
  t1 = wall_seconds ();
  timing->merging = t1 - t0;

  free (bucket_starts);
  free (bounds);
  free (pivots);
  free (samples);
  free (scratch);
}