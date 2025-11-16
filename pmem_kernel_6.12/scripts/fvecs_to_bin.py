#!/usr/bin/env python3
import os
import struct
from pathlib import Path

# =============================================================================
# USER CONFIGURATION
# =============================================================================

# Input .fvecs paths (original SIFT1M dataset)
BASE_FVECS = "/fast-lab-share/benchmarks/VectorDB/ANN/sift1m/base.fvecs"
QUERY_FVECS = "/fast-lab-share/benchmarks/VectorDB/ANN/sift1m/query.fvecs"

# Output .bin paths
BASE_BIN_OUT = "/home/lifan3/cxl_dist_cal/data/base.bin"
QUERY_BIN_OUT = "/home/lifan3/cxl_dist_cal/data/query.bin"

# Expected vector dimension for SIFT1M
EXPECTED_DIM = 128

# Optional limit for quick testing (set to None for full dataset)
LIMIT_BASE = None  # e.g., 10000 converts only 10k base vectors
LIMIT_QUERY = None  # e.g., 10 converts only 10 query vectors

# =============================================================================
# CONVERSION + META LOGIC
# =============================================================================

def convert_fvecs_to_bin(in_path, out_path, expected_dim, limit=None):
    vec_count = 0
    total_bytes = 0

    os.makedirs(os.path.dirname(out_path), exist_ok=True)

    with open(in_path, "rb") as f_in, open(out_path, "wb") as f_out:
        while True:
            dim_bytes = f_in.read(4)
            if not dim_bytes:
                break  # EOF

            if len(dim_bytes) != 4:
                raise RuntimeError(f"Corrupt file: incomplete dim at vector {vec_count}")

            (dim,) = struct.unpack("i", dim_bytes)
            if dim != expected_dim:
                raise ValueError(
                    f"Vector {vec_count} has dim={dim}, expected {expected_dim}"
                )

            data_bytes = f_in.read(dim * 4)
            if len(data_bytes) != dim * 4:
                raise RuntimeError(
                    f"Corrupt file: incomplete data for vector {vec_count}"
                )

            f_out.write(data_bytes)
            vec_count += 1
            total_bytes += len(data_bytes)

            if vec_count % 100000 == 0:
                print(f"  Converted {vec_count} vectors...")

            if limit is not None and vec_count >= limit:
                print(f"  Reached limit ({limit}), stopping early.")
                break

    # Create .meta file next to .bin
    meta_path = Path(out_path).with_suffix(".meta")
    with open(meta_path, "w") as meta:
        meta.write(f"vectors={vec_count}\n")
        meta.write(f"dimension={expected_dim}\n")
        meta.write(f"bytes={total_bytes}\n")
        meta.write(f"size_MB={total_bytes / (1024**2):.2f}\n")

    print(f"✅ Done: {vec_count} vectors written to {out_path}")
    print(f"   Meta info: {meta_path}")
    print(f"   Total size: {total_bytes / (1024**2):.2f} MB")

    return vec_count, total_bytes


def main():
    print("Converting base.fvecs → base.bin ...")
    convert_fvecs_to_bin(BASE_FVECS, BASE_BIN_OUT, EXPECTED_DIM, LIMIT_BASE)

    print("\nConverting query.fvecs → query.bin ...")
    convert_fvecs_to_bin(QUERY_FVECS, QUERY_BIN_OUT, EXPECTED_DIM, LIMIT_QUERY)


if __name__ == "__main__":
    main()
