#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "parser.h"
#include "sample_sort.h"

/* 
   : ------------------------------------------------------ :
   : MAIN                                                   :
   : ------------------------------------------------------ :
*/ 

static void print_summary (options_t *options, timing_t *timing, signature_t before_sig, signature_t after_sig, int sorted_ok, int signature_ok, size_t bad_index) {
  printf ("nkeys                    %zu\n", options->nkeys);
  printf ("n_bits                   %d\n", N_BITS);
  printf ("virtual_ranks            %u\n", options->nbuckets);
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
static void save_results(char *where_save, options_t *options, timing_t *timing, int sorted_ok, int signature_ok){
  FILE *file = fopen(where_save, "a+");
   if (file == NULL) {
        perror("fopen");
  }
  rewind(file);

  char *header = "n_key,n_bits,n_ranks,oversample,distribution,local_sort_algorithm,merging_strategy,seed,sorted_ok,multiset_signature_ok,time_generation_seconds,time_local_sort_seconds,time_sampling_seconds,time_partition_seconds,time_merge_seconds,time_verify_seconds,time_total_seconds\n";
  
  // checking for header existence
  char buffer[1024];

  rewind(file);

  int has_header = 0;

  if (fgets(buffer, sizeof(buffer), file) != NULL) {
      has_header = strcmp(buffer, header) == 0;
  }

  if (!has_header) {
      fputs(header, file);
  }

  fprintf (file, "%zu,", options->nkeys);
  fprintf (file, "%d,", N_BITS);
  fprintf (file, "%u,", options->nbuckets);
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
  options_t options;
  timing_t timing;
  sort_key_t *keys, *output;
  signature_t before_sig, after_sig;
  size_t bad_index;

  if (parse_options (argc, argv, &options) != 0) return EXIT_FAILURE;
  if (validate_options (&options) != 0) return EXIT_FAILURE;

  memset (&timing, 0, sizeof (timing));

  keys = malloc_array (options.nkeys, sizeof (sort_key_t));
  output = malloc_array (options.nkeys, sizeof (sort_key_t));

  double t_start = wall_seconds ();

  double t0 = wall_seconds ();
  generate_keys (keys, options.nkeys, &options);
  timing.generation = wall_seconds () - t0;

  t0 = wall_seconds ();
  before_sig = compute_signature (keys, options.nkeys);
  timing.signature = wall_seconds () - t0;

  sample_sort (keys, output, options.nkeys, &options, &timing);

  t0 = wall_seconds ();
  int sorted_ok = verify_sorted (output, options.nkeys, &bad_index);
  timing.sort_verification = wall_seconds () - t0;

  t0 = wall_seconds ();
  after_sig = compute_signature (output, options.nkeys);
  int signature_ok = same_signature (before_sig, after_sig);
  timing.signature_verification = wall_seconds () - t0;

  timing.total = wall_seconds () - t_start;

  print_summary (&options, &timing, before_sig, after_sig, sorted_ok, signature_ok, bad_index);
  print_key_prefix (output, options.nkeys, options.print_limit);

  save_results("./results/results.csv", &options, &timing, sorted_ok, signature_ok);

  free (output);
  free (keys);

  return (!sorted_ok || !signature_ok) ? EXIT_FAILURE : EXIT_SUCCESS;
}