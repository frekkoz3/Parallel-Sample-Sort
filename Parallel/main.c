#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "parser.h"
#include "sample_sort.h"
#include <mpi.h>

/* 
   : ------------------------------------------------------ :
   : MAIN                                                   :
   : ------------------------------------------------------ :
*/ 

static void print_summary (options_t *options, timing_t *timing, signature_t before_sig, signature_t after_sig, int sorted_ok, int signature_ok, size_t bad_index) {
  printf ("n_keys                   %zu\n", options->nkeys);
  printf ("n_bits                   %d\n", N_BITS);
  printf ("n_ranks                  %u\n", options->nbuckets);
  printf ("n_threads                %u\n", options->nthreads);
  printf ("oversample               %zu\n", options->oversample);
  printf ("distribution             %s\n", options->distribution_name);
  printf ("local sorting algorithm  %s\n", options->sorting_name);
  printf ("merging strategy         %s\n", options->merging_name);
  printf ("seed                     %" PRIu64 "\n", options->seed);
  printf ("sorted_ok                %s\n", sorted_ok ? "yes" : "no");
  printf ("multiset_signature_ok    %s\n", signature_ok ? "yes" : "no");
  printf ("input_signature_sum      %" PRIu64 "\n", before_sig.sum);
  printf ("input_signature_xor      %" PRIu64 "\n", before_sig.xor_value);
  printf ("output_signature_sum     %" PRIu64 "\n", after_sig.sum);
  printf ("output_signature_xor     %" PRIu64 "\n", after_sig.xor_value);
  if (!sorted_ok) printf ("first_bad_index          %zu\n", bad_index);
  printf ("time_generation_seconds  %.9f\n", timing->generation);
  printf ("time_local_sort_seconds  %.9f\n", timing->local_sort);
  printf ("time_sampling_seconds    %.9f\n", timing->sampling);
  printf ("time_partition_seconds   %.9f\n", timing->partitioning);
  printf ("time_merge_seconds       %.9f\n", timing->merging);
  printf ("time_verify_seconds      %.9f\n", timing->sort_verification);
  printf ("time_total_seconds       %.9f\n", timing->total);
}

// utility to save results
// the results will be later used on python side for analysis and visualization purposes
static void save_results(options_t *options, timing_t *timing, int sorted_ok, int signature_ok){

  FILE *file = fopen(options->where_save, "a+");
   if (file == NULL) {
        perror("fopen");
  }
  rewind(file);
  /* // removing it since it creates problem
  char *header = "n_key,n_bits,n_ranks,n_threads,oversample,distribution,local_sort_algorithm,merging_strategy,seed,sorted_ok,multiset_signature_ok,time_generation_seconds,time_local_sort_seconds,time_sampling_seconds,time_partition_seconds,time_merge_seconds,time_verify_seconds,time_total_seconds\n";
  int len = strlen(header);
  // checking for header existence
  char buffer[2*len]; // a little extra char for possible special characters

  rewind(file);

  int has_header = 0;

  if (fgets(buffer, sizeof(buffer), file) != NULL) {
      has_header = strcmp(buffer, header) == 0;
  }

  if (!has_header) {
      fputs(header, file);
  }
  */

  fprintf (file, "%zu,", options->nkeys);
  fprintf (file, "%d,", N_BITS);
  fprintf (file, "%u,", options->nbuckets);
  fprintf (file, "%u,", options->nthreads);
  fprintf (file, "%zu,", options->oversample);
  fprintf (file, "%s,", options->distribution_name);
  fprintf (file, "%s,", options->sorting_name);
  fprintf (file, "%s,", options->merging_name);
  fprintf (file, "%" PRIu64 ",", options->seed);
  fprintf (file, "%s,", sorted_ok ? "yes" : "no");
  fprintf (file, "%s,", signature_ok ? "yes" : "no");
  fprintf (file, "%.9f,", timing->generation);
  fprintf (file, "%.9f,", timing->local_sort);
  fprintf (file, "%.9f,", timing->sampling);
  fprintf (file, "%.9f,", timing->partitioning);
  fprintf (file, "%.9f,", timing->merging);
  fprintf (file, "%.9f,", timing->sort_verification);
  fprintf (file, "%.9f\n", timing->total);

  fflush(file);
  fclose(file);
}

int main (int argc, char **argv) {

  MPI_Init(&argc, &argv);

  int rank, nranks;

  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nranks);

  options_t options;
  timing_t timing;
  sort_key_t *keys, *output;
  signature_t before_sig, after_sig;
  size_t bad_index;

  if (parse_options (argc, argv, &options) != 0) {
    MPI_Finalize();
    return EXIT_FAILURE;
  }
  // Automatically set nbuckets to the number of MPI ranks provided by mpirun
  options.nbuckets = (unsigned int)nranks;
  if (validate_options (&options) != 0)  {
    MPI_Finalize();
    return EXIT_FAILURE;
  }
  options.seed += rank; // in this way each rank has different data

  memset (&timing, 0, sizeof (timing));

  // select the right number of keys for each rank
  size_t local_nkeys = options.nkeys/nranks; // just a placeholder
  int remaining_keys = options.nkeys%nranks;
  for (int i = 0; i < remaining_keys; i++){
    if (i == rank) local_nkeys++;// round robin allocations of the remaining keys
  }

  size_t *out_nkeys_all = malloc_array(nranks, sizeof (size_t)); // this contains the dimension of each final bucket
  // which will be used for the global offset computation

  keys = malloc_array (local_nkeys, sizeof (sort_key_t));
  output = NULL; // this will be allocated later in sample sort

  double t_start = MPI_Wtime ();

  double t0 = MPI_Wtime ();
  generate_keys (keys, local_nkeys, &options);
  timing.generation = MPI_Wtime () - t0;

  double t_max_elapsed;
  // A parallel step is only as fast as its slowest process.
  MPI_Reduce(&timing.generation, &t_max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

  if (rank == 0) {
      timing.generation = t_max_elapsed;
  }

  t0 = MPI_Wtime ();
  before_sig = compute_signature (keys, local_nkeys);
  timing.signature = MPI_Wtime () - t0;
  // A parallel step is only as fast as its slowest process.
  MPI_Reduce(&timing.signature, &t_max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  signature_t before_sig_all;
  MPI_Reduce(&before_sig.sum, &before_sig_all.sum, 1, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&before_sig.xor_value, &before_sig_all.xor_value, 1, MPI_UINT64_T, MPI_BXOR, 0, MPI_COMM_WORLD);

  if (rank == 0) {
      timing.signature = t_max_elapsed;
  }

  sample_sort (keys, &output, local_nkeys, out_nkeys_all, &options, &timing);
  
  // this is needed just for the local sort comparison
  if (nranks == 1){
    
    // skip this assuming everything fine (but just because is working in the other cases)
    timing.partitioning = timing.merging = timing.signature_verification = timing.sort_verification = 0.0;
    timing.total = timing.local_sort + timing.generation;
    signature_t after_sig_all = before_sig_all;
    int sorted_ok = 1;
    int signature_ok = 1;
    size_t global_bad_index = 0;
    print_summary (&options, &timing, before_sig_all, after_sig_all, sorted_ok, signature_ok, global_bad_index);
    print_key_prefix (output, local_nkeys, options.print_limit);
    save_results(&options, &timing, sorted_ok, signature_ok);
    free (keys);
    MPI_Finalize();
    return EXIT_SUCCESS;
  }

  // now we must recompute the global offset!!!
  // because once the sample sort is happened different chunks have different sizes
  MPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, out_nkeys_all, 1, MPI_SIZE_T, MPI_COMM_WORLD);
  // MPI_Allgather(&out_nkeys_all[rank], 1, MPI_SIZE_T, out_nkeys_all, 1, MPI_SIZE_T, MPI_COMM_WORLD);
  // why not this? why MPI_IN_PLACE? without it, it throw "Fatal error in internal_Allgather: Buffers must not be aliased".
  // this is caused by the fact that out_nkeys_all is passed as the receiving buffer (recvbuf),
  // while &out_nkeys_all[rank] is passed as the sending buffer (sendbuf). This is strictly forbidden
  // in MPI standard. (sendbuf and recvbuf cannot point to overlapping memory regions in standard collective calls)
  // MPI_IN_PLACE exactly permit to do the opposite. Why is it safe to do it?
  // in this case the indexex that could produce a data race are already safe.
  
  size_t global_offset = 0;
  for (int r = 0; r < rank; r++)
    global_offset += out_nkeys_all[r];
  size_t out_nkeys = out_nkeys_all[rank];

  t0 = MPI_Wtime();
  int local_err = 1 - verify_sorted(output, out_nkeys, &bad_index); // 1 if a local error occours, 0 otherwise
  bad_index = (local_err == 0) ? options.nkeys + 1 : bad_index + global_offset; // shifting it wrt to the rank size
  // if no bad index we set the bad index to the maximum number of keys so the minimum reduction does not create problem

  int boundary_err = 0;
  sort_key_t following_first;
  MPI_Request reqs[2];
  int n_reqs = 0;

  // post non-blocking receive from the next rank if we aren't the last rank
  if (rank < nranks - 1) {
    MPI_Irecv(&following_first, 1, MPI_SORT_KEY_T, rank + 1, 0, MPI_COMM_WORLD, &reqs[n_reqs++]);
  }

  // post non-blocking send to the previous rank if we aren't rank 0
  if (rank > 0) {
    MPI_Isend(&output[0], 1, MPI_SORT_KEY_T, rank - 1, 0, MPI_COMM_WORLD, &reqs[n_reqs++]);
  }

  // wait for all non-blocking communications to complete
  if (n_reqs > 0) {
    MPI_Waitall(n_reqs, reqs, MPI_STATUSES_IGNORE);
  }

  // following_first contains valid data, and output[0] has been safely sent
  if (rank < nranks - 1) {
    if (out_nkeys > 0 && output[out_nkeys - 1] > following_first) {
      boundary_err = 1;
      bad_index = global_offset + out_nkeys; // first index of the following rank
    }
  }

  size_t global_bad_index = 0;
  MPI_Reduce(&bad_index, &global_bad_index, 1, MPI_SIZE_T, MPI_MIN, 0, MPI_COMM_WORLD);

  timing.sort_verification = MPI_Wtime() - t0;

  int total_local_errs = 0;
  int total_boundary_errs = 0;

  MPI_Reduce(&local_err, &total_local_errs, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&boundary_err, &total_boundary_errs, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&timing.sort_verification, &t_max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

  int sorted_ok = 1;
  if (rank == 0) {
    timing.sort_verification = t_max_elapsed;
    sorted_ok = (total_local_errs == 0) && (total_boundary_errs == 0);
  }

  t0 = MPI_Wtime();
  after_sig = compute_signature(output, out_nkeys);

  signature_t after_sig_all;
  MPI_Reduce(&after_sig.sum, &after_sig_all.sum, 1, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&after_sig.xor_value, &after_sig_all.xor_value, 1, MPI_UINT64_T, MPI_BXOR, 0, MPI_COMM_WORLD);

  timing.signature_verification = MPI_Wtime() - t0;
  MPI_Reduce(&timing.signature_verification, &t_max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

  int signature_ok = 1;
  if (rank == 0) {
    timing.signature_verification = t_max_elapsed;
    signature_ok = same_signature(before_sig_all, after_sig_all);
  }

  timing.total = MPI_Wtime() - t_start;
  MPI_Reduce(&timing.total, &t_max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  if (rank == 0){
    timing.total = t_max_elapsed;
  }

  if (rank == 0){
    print_summary (&options, &timing, before_sig_all, after_sig_all, sorted_ok, signature_ok, global_bad_index);
    print_key_prefix (output, out_nkeys, options.print_limit);
    save_results(&options, &timing, sorted_ok, signature_ok);
  }

  free (output);
  free (keys);

  MPI_Finalize();

  return (!sorted_ok || !signature_ok) ? EXIT_FAILURE : EXIT_SUCCESS;
}