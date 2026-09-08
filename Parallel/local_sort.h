#ifndef LOCAL_SORT_H
#define LOCAL_SORT_H

#include "utils.h"

#define RADIX_BITS 11
#define NBUCKETS (1 << RADIX_BITS) // 256 buckets
#define MASK (NBUCKETS - 1)

// MERGE SORT

size_t chunk_begin (const size_t nkeys, const unsigned int nchunks, const unsigned int chunk_id);
size_t chunk_end (const size_t nkeys, const unsigned int nchunks, const unsigned int chunk_id);
void merge_sort_omp (sort_key_t *data, sort_key_t *scratch, size_t begin, size_t end);
void radix_sort (sort_key_t *data, sort_key_t *scratch, size_t begin, size_t end);
void sort_virtual_chunks (sort_key_t *keys, sort_key_t *scratch, size_t nkeys, unsigned int nchunks, base_sorting sort_algo);

#endif /* LOCAL_SORT_H */