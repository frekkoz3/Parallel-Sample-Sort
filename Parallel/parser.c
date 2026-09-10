#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

/* 
   : ------------------------------------------------------ :
   : PARSER                                                 :
   : ------------------------------------------------------ :
*/ 

/*
  Print command-line usage.
*/
void
print_usage ( char     *program_name   // executable name from argv[0]
	      )
{
  fprintf (stderr,
           "Usage: %s [options]\n\n"
           "Options:\n"
           "  --n VALUE              number of keys to sort                    (%llu)\n"
           "  --nbuckets VALUE       number of virtual ranks / buckets          (%u)\n"
           "  --oversample VALUE     regular samples per virtual rank multiplier (%llu)\n"
           "  --seed VALUE           random seed for generated inputs            (%llu)\n"
           "  --distribution NAME    uniform | skewed | few-unique | sorted | reverse | almost-sorted (%s)\n"
           "  --sorting NAME         merge | quick | radix (%s)\n"
           "  --radix_bits VALUE     digit size for radix sort (%d)\n"
           "  --merging_strat NAME   final merging strategy basic | bin | heap | tournament (%s)\n"
           "  --print-limit VALUE    print the first VALUE sorted keys           (%llu)\n"
           "  --help                 show this help message\n\n",
           program_name, (unsigned long long) DEFAULT_NKEYS, DEFAULT_NBUCKETS,
           (unsigned long long) DEFAULT_OVERSAMPLE, (unsigned long long) DEFAULT_SEED,
           DEFAULT_DISTRIBUTION, DEFAULT_SORT, DEFAULT_RADIX_BITS, DEFAULT_MERGING_STRAT ,(unsigned long long) DEFAULT_PRINT_LIMIT);
}

/*
  Fill the option structure with defaults.
*/
static void
set_default_options ( options_t   *options   // output options structure
		      )
{
  options->nkeys             = (size_t) DEFAULT_NKEYS;
  options->nbuckets          = DEFAULT_NBUCKETS;
  options->oversample        = (size_t) DEFAULT_OVERSAMPLE;
  options->seed              = DEFAULT_SEED;
  options->distribution      = DISTRIBUTION_UNIFORM;
  options->distribution_name = DEFAULT_DISTRIBUTION;
  options->sorting           = MERGE_SORT;
  options->sorting_name      = DEFAULT_SORT;
  options->radix_bits        = DEFAULT_RADIX_BITS;
  options->merging           = BINARY_ITERATIVE_KWM;
  options->merging_name      = DEFAULT_MERGING_STRAT;
  options->print_limit       = (size_t) DEFAULT_PRINT_LIMIT;
}

/*
  Convert a distribution name into the internal enum.  
  Different types of data distribution are useful for a load-imbalance discussion:
  uniform is the clean case, skewed and few-unique stress the pivot selection,
  and sorted/reverse are edge cases for the local sort.
*/
static int
parse_distribution_name (char           *name,          // user-provided distribution name
                         distribution_t *distribution   // parsed distribution enum
			 )
{
  if (strcmp (name, "uniform") == 0) { *distribution = DISTRIBUTION_UNIFORM; return 0; }
  if (strcmp (name, "skewed") == 0) { *distribution = DISTRIBUTION_SKEWED; return 0; }
  if (strcmp (name, "few-unique") == 0 || strcmp (name, "fewunique") == 0) { *distribution = DISTRIBUTION_FEW_UNIQUE; return 0; }
  if (strcmp (name, "sorted") == 0) { *distribution = DISTRIBUTION_SORTED; return 0; }
  if (strcmp (name, "reverse") == 0) { *distribution = DISTRIBUTION_REVERSE; return 0; }
  if (strcmp (name, "almost-sorted") == 0 || strcmp (name, "almostsorted") == 0) { *distribution = DISTRIBUTION_ALMOST_SORTED; return 0; }
  fprintf (stderr, "Unknown distribution '%s'\n", name);
  return -1;
}

/*
  Convert a sorting name into the internal enum.  
*/
static int parse_sort_name (char           *name,          // user-provided distribution name
                            base_sorting   *sorting        // parsed sorting enum
                            )
{
  if (strcmp (name, "merge") == 0) { *sorting = MERGE_SORT; return 0; }
  if (strcmp (name, "quick") == 0) { *sorting = QUICK_SORT; return 0; }
  if (strcmp (name, "radix") == 0) { *sorting = RADIX_SORT; return 0; }
  fprintf (stderr, "Unknown distribution '%s'\n", name);
  return -1;
}

/*
  Convert a distribution name into the internal enum.  
  Different types of data distribution are useful for a load-imbalance discussion:
  uniform is the clean case, skewed and few-unique stress the pivot selection,
  and sorted/reverse are edge cases for the local sort.
*/
static int
parse_merging_name (char             *name,          // user-provided distribution name
                   merging_strategy *merging       // parsed distribution enum
			 )
{
  if (strcmp (name, "basic") == 0) { *merging = BASIC_ITERATIVE_KWM; return 0; }
  if (strcmp (name, "bin") == 0) { *merging = BINARY_ITERATIVE_KWM; return 0; }
  if (strcmp (name, "heap") == 0) { *merging = HEAP_DIRECT_KWM; return 0; }
  if (strcmp (name, "tournament") == 0) { *merging = TORUNAMENT_TREE_DIRECT_KWM; return 0; }
  fprintf (stderr, "Unknown distribution '%s'\n", name);
  return -1;
}

/*
  Parse an unsigned integer option into size_t.
  This helper keeps the command line parsing in main-style code readable
  and centralises overflow checks.
*/
static int
parse_size_option ( int      argc,    // number of command-line tokens
                    char   **argv,    // command-line token vector
                    int     *i,       // index of the option being parsed
                    size_t  *value    // parsed output value
		    )
{
  char *endptr;
  if (*i + 1 >= argc) { fprintf (stderr, "Missing value after %s\n", argv[*i]); return -1; }
  errno = 0; endptr = NULL;
  unsigned long long parsed = strtoull (argv[*i + 1], &endptr, 10);
  if (errno != 0 || endptr == argv[*i + 1] || *endptr != '\0') {
    fprintf (stderr, "Invalid unsigned integer for %s: %s\n", argv[*i], argv[*i + 1]);
    return -1;
  }
  if (parsed > (unsigned long long) SIZE_MAX) {
    fprintf (stderr, "Value too large for this platform: %s\n", argv[*i + 1]);
    return -1;
  }
  *value = (size_t) parsed;
  *i += 1;
  return 0;
}

/*
  Parse an unsigned integer option into unsigned int.
*/
static int
parse_uint_option ( int            argc,    // number of command-line tokens
                    char         **argv,    // command-line token vector
                    int           *i,       // index of the option being parsed
                    unsigned int  *value    // parsed output value
		    )
{
  size_t parsed;
  if (parse_size_option (argc, argv, i, &parsed) != 0) return -1;
  if (parsed > (size_t) UINT_MAX) {
    fprintf (stderr, "Value too large for unsigned int: %zu\n", parsed);
    return -1;
  }
  *value = (unsigned int) parsed;
  return 0;
}

/*
  Parse an unsigned integer option into uint64_t.
*/
static int
parse_u64_option ( int        argc,    // number of command-line tokens
                   char     **argv,    // command-line token vector
                   int       *i,       // index of the option being parsed
                   uint64_t  *value    // parsed output value
		   ) {
  char *endptr;
  if (*i + 1 >= argc) { fprintf (stderr, "Missing value after %s\n", argv[*i]); return -1; }
  errno = 0; endptr = NULL;
  unsigned long long parsed = strtoull (argv[*i + 1], &endptr, 10);
  if (errno != 0 || endptr == argv[*i + 1] || *endptr != '\0') {
    fprintf (stderr, "Invalid unsigned integer for %s: %s\n", argv[*i], argv[*i + 1]);
    return -1;
  }
  *value = (uint64_t) parsed;
  *i += 1;
  return 0;
}

/*
  Parse an unsigned integer option into uint64_t.
*/
static int
parse_int_option ( int        argc,    // number of command-line tokens
                   char     **argv,    // command-line token vector
                   int       *i,       // index of the option being parsed
                   int       *value    // parsed output value
		   ) {
  char *endptr;
  if (*i + 1 >= argc) { fprintf (stderr, "Missing value after %s\n", argv[*i]); return -1; }
  errno = 0; endptr = NULL;
  unsigned long long parsed = strtoull (argv[*i + 1], &endptr, 10);
  if (errno != 0 || endptr == argv[*i + 1] || *endptr != '\0') {
    fprintf (stderr, "Invalid integer for %s: %s\n", argv[*i], argv[*i + 1]);
    return -1;
  }
  *value = (int) parsed;
  *i += 1;
  return 0;
}

/*
  Parse all command-line options.
  Unknown flags are treated as errors so that mistakes in job scripts are caught
*/
int
parse_options ( int          argc,      // number of command-line tokens
                char       **argv,      // command-line token vector
                options_t   *options    // output options structure
	      )
{
  set_default_options (options);
  for (int i = 1; i < argc; i++) {
    if (strcmp (argv[i], "--help") == 0) { print_usage (argv[0]); exit (EXIT_SUCCESS); }
    else if (strcmp (argv[i], "--n") == 0) { if (parse_size_option (argc, argv, &i, &options->nkeys) != 0) return -1; }
    else if (strcmp (argv[i], "--nbuckets") == 0) { if (parse_uint_option (argc, argv, &i, &options->nbuckets) != 0) return -1; }
    else if (strcmp (argv[i], "--oversample") == 0) { if (parse_size_option (argc, argv, &i, &options->oversample) != 0) return -1; }
    else if (strcmp (argv[i], "--seed") == 0) { if (parse_u64_option (argc, argv, &i, &options->seed) != 0) return -1; }
    else if (strcmp (argv[i], "--distribution") == 0) {
      if (i + 1 >= argc) return -1;
      if (parse_distribution_name (argv[i + 1], &options->distribution) != 0) return -1;
      options->distribution_name = argv[i + 1]; i++;
    }
    else if (strcmp (argv[i], "--sorting") == 0) {
      if (i + 1 >= argc) return -1;
      if (parse_sort_name (argv[i + 1], &options->sorting) != 0) return -1;
      options->sorting_name = argv[i + 1]; i++;
    }
    else if (strcmp (argv[i], "--radix_bits") == 0) { if (parse_int_option (argc, argv, &i, &options->radix_bits) != 0) return -1; }
    else if (strcmp (argv[i], "--merging_strat") == 0) {
      if (i + 1 >= argc) return -1;
      if (parse_merging_name (argv[i + 1], &options->merging) != 0) return -1;
      options->merging_name = argv[i + 1]; i++;
    }
    else if (strcmp (argv[i], "--print-limit") == 0) { if (parse_size_option (argc, argv, &i, &options->print_limit) != 0) return -1; }
    else { fprintf (stderr, "Unknown option: %s\n", argv[i]); print_usage (argv[0]); return -1; }
  }
  return 0;
}

/*
  Validate the option values after parsing.
  In real codes you should always validate the input, t avoid wasting time
  with runs that either crash or are non-sense
*/
int
validate_options ( options_t   *options   // parsed options to validate
		   )
{
  if (options->nkeys == 0 || options->nbuckets == 0 || options->oversample == 0) return -1;
  if ((size_t) options->nbuckets > options->nkeys) return -1;
  if ((size_t) options->radix_bits > N_BITS) return -1; // maximum number of digits < N_BITS
  if (N_BITS % (size_t) options->radix_bits != 0) return -1; // the radix_bits must be a divisor of N_BITS
  if (options->nbuckets > 1) {
    if (options->oversample > SIZE_MAX / (size_t) (options->nbuckets - 1)) return -1;
    size_t samples_per_chunk = options->oversample * (size_t) (options->nbuckets - 1);
    if (samples_per_chunk > SIZE_MAX / (size_t) options->nbuckets) return -1;
  }
  return 0;
}