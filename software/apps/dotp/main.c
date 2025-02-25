// Copyright 2021 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

// Author: Marco Bertuletti, ETH Zurich

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "encoding.h"
#include "printf.h"
#include "runtime.h"
#include "synchronization.h"

#include "define.h"

#include "dotp_parallel.h"
#include "dotp_parallel_local.h"
#include "dotp_parallel_red0.h"
#include "dotp_parallel_redtree.h"
#include "dotp_single.h"

void init_vectors(int32_t *in_a, int32_t *in_b, int32_t *s, int32_t *p_result,
                  int32_t *p_check, uint32_t Len) {
  *p_result = 0;
  *p_check = 0;
  uint32_t j = 0;
  while (j < Len) {
    int32_t a = (int32_t)(j % NUM_CORES);
    int32_t b = (int32_t)(j % 4 + 3);
    in_a[j] = a;
    in_b[j] = b;
    *p_check = *p_check + (int32_t)(a * b);
    j++;
  }
#if defined(PARALLEL_RED0) || defined(PARALLEL_UNROLLED_RED0) ||               \
    defined(PARALLEL_REDTREE) || defined(PARALLEL_UNROLLED_REDTREE)
  for (uint32_t k = 0; k < N_BANK; k++) {
    s[k] = 0;
    red_barrier[k] = 0;
  }
#else
  *s = 0;
#endif
}

int main() {

  uint32_t core_id = mempool_get_core_id();
  uint32_t time_init, time_end;
  // initialize synchronization variables
  mempool_barrier_init(core_id);

  if (core_id == 0) {
    error = 0;
    time_init = 0;
    time_end = 0;
#if defined(PARALLEL_RED0) || defined(PARALLEL_UNROLLED_RED0) ||               \
    defined(PARALLEL_REDTREE) || defined(PARALLEL_UNROLLED_REDTREE)
    init_vectors(vector_a, vector_b, sum, &result, &check, LEN);
#else
    init_vectors(vector_a, vector_b, &sum, &result, &check, LEN);
#endif
  }
  mempool_barrier(NUM_CORES); // wait until all cores have finished

  // Kernel execution

  time_init = mempool_get_timer();
#ifdef SINGLE
  dotp_single(vector_a, vector_b, &sum, LEN);
#elif defined(SINGLE_UNROLLED)
  dotp_single_unrolled4(vector_a, vector_b, &sum, LEN);
#endif
  time_end = mempool_get_timer();

  time_init = mempool_get_timer();
  mempool_start_benchmark();
/* A) Parallelized workload
   B) Atomic fetch and add to a single memory location
   C) Barrier */
#ifdef PARALLEL
  dotp_parallel(vector_a, vector_b, &sum, LEN, N_PE);
#elif defined(PARALLEL_UNROLLED)
  dotp_parallel_unrolled4(vector_a, vector_b, &sum, LEN, N_PE);
/* A) Parallelized workload
   B) Atomic fetch and add to local memory banks
   C) Barrier
   D) Final reduction by core 0 incorporated in a barrier */
#elif defined(PARALLEL_RED0)
  dotp_parallel_red0(vector_a, vector_b, sum, LEN, N_PE);
#elif defined(PARALLEL_UNROLLED_RED0)
  dotp_parallel_unrolled4_red0(vector_a, vector_b, sum, LEN, N_PE);
/* A) Parallelized workload
   B) Nested set of barriers: reduction is performed in a logarithmic tree. */
#elif defined(PARALLEL_REDTREE)
  dotp_parallel_redtree(vector_a, vector_b, sum, LEN, N_PE);
#elif defined(PARALLEL_UNROLLED_REDTREE)
  dotp_parallel_redtree_unrolled(vector_a, vector_b, sum, LEN, N_PE);
#endif
  mempool_stop_benchmark();
  time_end = mempool_get_timer();

  /* A) Parallelized workload
     B) Atomic fetch and add to a single memory location
     C) Barrier */
  if (core_id < N_PE) {
    time_init = mempool_get_timer();
    mempool_start_benchmark();
#ifdef PARALLEL_LOCAL
    dotp_parallel_local(vector_a, vector_b, &sum, LEN, N_PE);
#elif defined(LOCAL_UNROLLED)
    dotp_parallel_local_unrolled4(vector_a, vector_b, &sum, LEN, N_PE);
#endif
    mempool_stop_benchmark();
    time_end = mempool_get_timer();
  }

  mempool_barrier(NUM_CORES);
  // Check results
  if (core_id == 0) {
    uint32_t clock_cycles = (time_end - time_init);
#if defined(PARALLEL_RED0) || defined(PARALLEL_UNROLLED_RED0) ||               \
    defined(PARALLEL_REDTREE) || defined(PARALLEL_UNROLLED_REDTREE)
    result = sum[0];
#else
    result = sum;
#endif
    printf("\nKernel execution takes %d clock cycles\n", clock_cycles);
    printf("Result ==> %d\n", result);
    printf("Check  ==> %d\n\n", check);
  }
  mempool_barrier(NUM_CORES);

  return error;
}
