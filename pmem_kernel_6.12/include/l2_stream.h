#pragma once
#include <linux/types.h>

/**
 * Stream SIFT1M base vectors in batches and measure total cycles.
 * Returns 0 on success, <0 on error.
 */
int run_l2_streaming_from_file_v3(const char *base_path,
                               const char *query_path,
                               u64 total_vecs, u32 dim,
                               u64 batch_vecs, u32 clk_mhz,
                               int cxl_nid, u64 cxl_base);
