#include "sample_sort.h"
#include "local_sort.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include <inttypes.h>

/*
  Select regular samples from every sorted chunk.
  Regular sampling is later implemented before the MPI_ gatehring of samples; this
  serial function stores the gathered samples directly in one array.
*/
static void
select_regular_samples(sort_key_t    *keys,
                       size_t         nkeys,
                       size_t         samples_per_chunk,
                       sort_key_t    *samples)
{
    size_t s;
    size_t local_index;

    for (s = 1; s <= samples_per_chunk; s++)
    {
        local_index =
            (s * nkeys) / (samples_per_chunk + 1);

        if (local_index >= nkeys)
            local_index = nkeys - 1;

        samples[s - 1] = keys[local_index];
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
                       size_t         samples_per_chunk,   // samples contributed by each mpi rank
                       unsigned int   nbuckets,            // number of output buckets
                       sort_key_t    *pivots               // output array of nbuckets - 1 pivots
)
{
    unsigned int bucket;

    for (bucket = 1; bucket < nbuckets; bucket++)
    {
        size_t pivot_index = (size_t) bucket * samples_per_chunk;
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
  Partition every sorted chunk into nbuckets sorted subranges.
  In MPI this becomes the send counter and displacement arrays used by (possibly)
  MPI_Alltoall and MPI_Alltoallv.
*/
static void
build_bucket_bounds(sort_key_t    *keys,
                    size_t         nkeys,
                    unsigned int   nbuckets,
                    sort_key_t    *pivots,
                    size_t        *bounds)
{
    unsigned int bucket;
    size_t cursor;
    size_t pos;

    cursor = 0;
    bounds[0] = 0;

    for (bucket = 0; bucket + 1 < nbuckets; bucket++)
    {
        pos = upper_bound_key(keys, cursor, nkeys, pivots[bucket]);

        bounds[bucket + 1] = pos;
        cursor = pos;
    }

    bounds[nbuckets] = nkeys;
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
                                      sort_key_t    *output        // full output array
			 )
{
  size_t      *current;
  size_t      *end;
  unsigned int source;
  unsigned int best_source;
  int          have_best;
  sort_key_t   best_key;
  size_t       out;
  size_t       total_keys;

  current = malloc_array ((size_t) nchunks, sizeof (size_t));
  end = malloc_array ((size_t) nchunks, sizeof (size_t));

  for (source = 0; source < nchunks; source++)
    {
      current[source] = bounds[source];
      end[source] = bounds[source + 1];
    }

  total_keys = bounds[nchunks];
  out = 0;

  while (out < total_keys)
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
*/
static void
binary_iterative_k_way_merge_buckets ( sort_key_t    *keys,          // source sorted chunks
                                        size_t        *bounds,        // source/destination boundaries
                                        unsigned int   nchunks,       // number of incoming streams
                                        sort_key_t    *output        // full output array
                                      )
{
  if (nchunks == 0) return;
  
  size_t total_keys = bounds[nchunks];
  if (total_keys == 0) return;

  if (nchunks == 1)
    {
      memcpy (output, keys, total_keys * sizeof (sort_key_t));
      return;
    }

  // Allocate working buffer and stream boundaries for ping-pong rounds
  sort_key_t *scratch = malloc_array (total_keys, sizeof (sort_key_t));
  size_t     *cur_bounds = malloc_array ((size_t) nchunks + 1, sizeof (size_t));
  size_t     *next_bounds = malloc_array ((size_t) nchunks + 1, sizeof (size_t));

  memcpy (cur_bounds, bounds, (nchunks + 1) * sizeof (size_t));

  sort_key_t *src_buf = keys;
  sort_key_t *dst_buf = scratch;
  unsigned int active_chunks = nchunks;

  while (active_chunks > 1)
    {
      unsigned int next_chunks = (active_chunks + 1) / 2;
      next_bounds[0] = 0;

      for (unsigned int i = 0; i < active_chunks; i += 2)
        {
          unsigned int out_idx = i / 2;

          if (i + 1 < active_chunks)
            {
              // 2-way merge of stream i and stream i+1
              size_t p1 = cur_bounds[i];
              size_t end1 = cur_bounds[i + 1];
              size_t p2 = cur_bounds[i + 1];
              size_t end2 = cur_bounds[i + 2];
              size_t out_pos = next_bounds[out_idx];

              while (p1 < end1 && p2 < end2)
                {
                  if (src_buf[p1] <= src_buf[p2])
                    dst_buf[out_pos++] = src_buf[p1++];
                  else
                    dst_buf[out_pos++] = src_buf[p2++];
                }
              while (p1 < end1) dst_buf[out_pos++] = src_buf[p1++];
              while (p2 < end2) dst_buf[out_pos++] = src_buf[p2++];

              next_bounds[out_idx + 1] = out_pos;
            }
          else
            {
              // Odd chunk out: direct copy to destination
              size_t p1 = cur_bounds[i];
              size_t end1 = cur_bounds[i + 1];
              size_t out_pos = next_bounds[out_idx];
              size_t len = end1 - p1;

              memcpy (&dst_buf[out_pos], &src_buf[p1], len * sizeof (sort_key_t));
              next_bounds[out_idx + 1] = out_pos + len;
            }
        }

      // Prepare for next pass
      active_chunks = next_chunks;
      memcpy (cur_bounds, next_bounds, (active_chunks + 1) * sizeof (size_t));

      // Ping-pong buffers
      src_buf = dst_buf;
      dst_buf = (src_buf == scratch) ? output : scratch;
    }

  // If the final result ended up in scratch, copy it to output
  if (src_buf != output)
    {
      memcpy (output, scratch, total_keys * sizeof (sort_key_t));
    }

  free (scratch);
  free (cur_bounds);
  free (next_bounds);
}

/*
  Heap direct K-way merge.
  We store a min-heap containing the minimum from each chunk.
  We exctract the minimum from the heap. This minimum belong to a certain chunk, 
  so we then proceed to add the following minimum of that chunk to the heap.
*/
typedef struct {
  sort_key_t   key;
  unsigned int source;
} heap_node_t;

static void
sift_down_heap (heap_node_t *heap, size_t size, size_t idx)
{
  size_t min_idx = idx;

  while (1)
    {
      size_t left  = 2 * idx + 1;
      size_t right = 2 * idx + 2;

      if (left < size && heap[left].key < heap[min_idx].key)
        min_idx = left;
      if (right < size && heap[right].key < heap[min_idx].key)
        min_idx = right;

      if (min_idx != idx)
        {
          heap_node_t tmp = heap[idx];
          heap[idx]       = heap[min_idx];
          heap[min_idx]   = tmp;
          idx             = min_idx;
        }
      else
        {
          break;
        }
    }
}

static void
heap_direct_k_way_merge_buckets ( sort_key_t    *keys,          // source sorted chunks
                                  size_t        *bounds,        // source/destination boundaries
                                  unsigned int   nchunks,       // number of incoming streams
                                  sort_key_t    *output         // full output array
                                )
{
  if (nchunks == 0) return;

  size_t      *current = malloc_array ((size_t) nchunks, sizeof (size_t));
  heap_node_t *heap    = malloc_array ((size_t) nchunks, sizeof (heap_node_t));
  size_t       heap_size = 0;
  size_t       out = 0;

  for (unsigned int s = 0; s < nchunks; s++)
    {
      current[s] = bounds[s];
      if (current[s] < bounds[s + 1])
        {
          heap[heap_size].key    = keys[current[s]++];
          heap[heap_size].source = s;
          heap_size++;
        }
    }

  // Build min-heap
  if (heap_size > 0)
    {
      for (int i = (int)(heap_size / 2) - 1; i >= 0; i--)
        sift_down_heap (heap, heap_size, (size_t) i);
    }

  // Extract min and push next element from the same stream
  while (heap_size > 0)
    {
      output[out++] = heap[0].key;
      unsigned int src = heap[0].source;

      if (current[src] < bounds[src + 1])
        {
          heap[0].key = keys[current[src]++];
          sift_down_heap (heap, heap_size, 0);
        }
      else
        {
          heap[0] = heap[heap_size - 1];
          heap_size--;
          if (heap_size > 0)
            sift_down_heap (heap, heap_size, 0);
        }
    }

  free (current);
  free (heap);
}

/*
  Merge every destination bucket in increasing pivot order.
  Concatenating these merged buckets gives the final globally sorted array because all keys in
  bucket b are less than or equal to all keys in bucket b + 1 by construction.
*/
static void
merge_local_destination_buckets( sort_key_t        *keys,                // sorted source chunks
                                  size_t            *local_bounds,              // source/destination boundaries
                                  unsigned int       nchunks,             // number of mpi_ranks
                                  sort_key_t        *output,              // sorted output array
                                  merging_strategy   merging_strat        // k-way merging strategy
		  )
{
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (merging_strat == BINARY_ITERATIVE_KWM) {
    binary_iterative_k_way_merge_buckets (keys, local_bounds, nchunks, output);
  }
  else if (merging_strat == BASIC_ITERATIVE_KWM) {
   basic_iterative_k_way_merge_buckets (keys, local_bounds, nchunks, output);
  }
  else if (merging_strat == HEAP_DIRECT_KWM) {
    heap_direct_k_way_merge_buckets (keys, local_bounds, nchunks, output);
  }
}

/*
  Run the sample-sort algorithm.
  The input array is modified during the local sort phase;
  the final globally sorted sequence is written to output.
*/
void sample_sort (  sort_key_t    *keys,          // input keys, modified by local sorts
                    sort_key_t    **output,       // pointer to local (globally) sorted output keys
                    size_t         nkeys,         // number of keys
                    size_t        *out_nkeys,     // output dimension
                    options_t     *options,       // runtime options
                    timing_t      *timing         // phase timings to fill
      )
{
  sort_key_t *scratch;
  sort_key_t *samples;
  sort_key_t *pivots;
  size_t     *bounds;
  size_t      samples_per_chunk;
  size_t      nsamples;
  double      t0;
  double      t1;

  int rank, nranks;

  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nranks);

  scratch = malloc_array (nkeys, sizeof (sort_key_t));

  // ··············································
  // sort your local chunk

  t0 = MPI_Wtime();
  sort_rank (keys, scratch, nkeys, options);
  t1 = MPI_Wtime();
  timing->local_sort = t1 - t0;

  free (scratch);

  if (options->nbuckets == 1) { // remember n_buckets = number of mpi ranks!
    *output = keys;
    timing->sampling = timing->partitioning = timing->merging = 0.0;
    return;
  }

  
  // ··············································
  // sample

  samples_per_chunk = options->oversample * (size_t) (options->nbuckets);
  nsamples          = samples_per_chunk * (size_t) options->nbuckets;

  sort_key_t *local_samples = malloc_array(samples_per_chunk, sizeof(sort_key_t)); // local temporal buffer

  samples        = malloc_array (nsamples, sizeof (sort_key_t));
  pivots         = malloc_array ((size_t) options->nbuckets - 1, sizeof (sort_key_t));
  bounds         = malloc_array((size_t) options->nbuckets + 1, sizeof(size_t));

  t0 = MPI_Wtime();
  // here locally select the regular samples and then send the sample to a common receiver 
  // which must sort them and select global pivots -> common receiver = rank 0
  select_regular_samples(keys, nkeys, samples_per_chunk, local_samples);

  double t0_communication = MPI_Wtime();
  // we send the local sample to rank 0. it will sort them
  MPI_Gather(local_samples, samples_per_chunk, MPI_SORT_KEY_T, samples, samples_per_chunk, MPI_SORT_KEY_T, 0, MPI_COMM_WORLD);
  double tf_communication = (MPI_Wtime() - t0_communication);

  free(local_samples);

  if (rank == 0){
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
    // and we select the pivots
    choose_global_pivots (samples, samples_per_chunk, options->nbuckets, pivots);
  }

  free (samples);

  t0_communication = MPI_Wtime();
  // finally we send the pivots to all the ranks
  MPI_Bcast(pivots, (options->nbuckets - 1), MPI_SORT_KEY_T, 0, MPI_COMM_WORLD);
  tf_communication += MPI_Wtime() - t0_communication;

  t1 = MPI_Wtime();

  double t_max_elapsed;

  timing->sampling = t1 - t0;
  // A parallel step is only as fast as its slowest process.
  MPI_Reduce(&timing->sampling, &t_max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

  if (rank == 0) {
      timing->sampling = t_max_elapsed;
  }

  // ··············································
  // have a global view

  // now here locally we must form the various buckets 
  // wrt each different global pivot
  t0 = MPI_Wtime();
  build_bucket_bounds (keys, nkeys, options->nbuckets, pivots, bounds);
  t1 = MPI_Wtime ();
  timing->partitioning = t1 - t0;
  // A parallel step is only as fast as its slowest process.
  MPI_Reduce(&timing->partitioning, &t_max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  
  free (pivots);

  if (rank == 0) {
      timing->partitioning = t_max_elapsed;
  }
  
  // ··············································
  // merge
  // each mpi rank must handle all the bucket corresponding
  // to the pivot with its own mpi index
  // so here we will have a good Receive buckets from the others !!!
  // and of course a Send buckets to the others !!!
  // then we just k-way merge the received array into output
  // here begin probably the biggest bottleneck of the problem:
  // we must use a MPI_Alltoallv operation in order to 

  // start by computing the # of keys each rank send to every other rank.
  // the v stands exactly for "variable" number of ...
  int *send_counts = malloc_array(nranks, sizeof(int));
  int *send_offset = malloc_array(nranks, sizeof(int));
  int *recv_counts = malloc_array(nranks, sizeof(int));
  int *recv_offset = malloc_array(nranks, sizeof(int));

  /* Number of keys sent to each destination rank,
    and starting offset of each bucket. */
  for (int i = 0; i < nranks; i++)
  {
      send_counts[i] = (int)(bounds[i + 1] - bounds[i]);

      send_offset[i] = (int)bounds[i];
  }

  free (bounds);

  t0_communication = MPI_Wtime();
  // now w find out how many keys we receive from each rank
  MPI_Alltoall(send_counts, 1, MPI_INT, recv_counts, 1, MPI_INT, MPI_COMM_WORLD);
  tf_communication += (MPI_Wtime() - t0_communication);

  // we compute the prefix sum of receive counts
  recv_offset[0] = 0;

  for (int i = 1; i < nranks; i++)
  {
      recv_offset[i] = recv_offset[i - 1] + recv_counts[i - 1];
  }
  
  // we compute the total number of received keys
  int total_recv_keys =  recv_offset[nranks - 1] + recv_counts[nranks - 1];

  // temporal buffer to store the merged output
  sort_key_t *recv_buffer = malloc_array(total_recv_keys, sizeof(sort_key_t));

  // dynamic allocation of the output 
  *output = malloc_array(total_recv_keys, sizeof(sort_key_t));

  // this will be need later in the main 
  out_nkeys[rank] = total_recv_keys;

  t0_communication = MPI_Wtime();
  // here finally our MPI_Alltoallv

  // TOMORROW DECOMMENT THIS!!! THE EXPERIMENT SHOULD BE FINISHED RN 
  // YOU CAN USE THE RESULTS IN STRONG SCALING AS COMPARISON

  MPI_Alltoallv(keys, send_counts, send_offset, MPI_SORT_KEY_T, recv_buffer, recv_counts, recv_offset, MPI_SORT_KEY_T, MPI_COMM_WORLD);
  
  /*
  for (int step = 0; step < nranks; step++) {
    int send_to = (rank + step) % nranks;
    int recv_from = (rank - step + nranks) % nranks;

    MPI_Sendrecv(
        &keys[send_offset[send_to]], send_counts[send_to], MPI_SORT_KEY_T, send_to, 0,
        &recv_buffer[recv_offset[recv_from]], recv_counts[recv_from], MPI_SORT_KEY_T, recv_from, 0,
        MPI_COMM_WORLD, MPI_STATUS_IGNORE
    );
  }// alternative to test the point-to-point communication 
  // this alternative create a circular dependency, not optimized at all
  // so i expect performances (for large enough P) to deterior
  */

  tf_communication += (MPI_Wtime() - t0_communication);

  timing->communication = tf_communication;
  
  // each received segment corresponds to one source rank
  // we now compute each local bound
  size_t *local_bounds = malloc_array((size_t)nranks + 1, sizeof(size_t));

  local_bounds[0] = 0;

  for (int i = 0; i < nranks; i++)
  {
      local_bounds[i + 1] = local_bounds[i] + recv_counts[i];
  }

  t0 = MPI_Wtime();

  // we finally merge the received streams from all the ranks
  merge_local_destination_buckets(recv_buffer, local_bounds, options->nbuckets, *output, options->merging);

  t1 = MPI_Wtime();
  timing->merging = t1 - t0;
  // A parallel step is only as fast as its slowest process.
  MPI_Reduce(&timing->merging, &t_max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

  if (rank == 0) {
      timing->merging = t_max_elapsed;
  }
  
  free (send_counts);
  free (send_offset);
  free (recv_counts);
  free (recv_offset);
  free (recv_buffer);
  free (local_bounds);
}