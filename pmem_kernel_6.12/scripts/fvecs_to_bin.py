#!/usr/bin/env python3
import os
import struct
from pathlib import Path

import numpy as np

# =============================================================================
# USER CONFIGURATION
# =============================================================================

# Input .fvecs paths (original SIFT1M dataset)
BASE_FVECS = "/fast-lab-share/benchmarks/VectorDB/ANN/sift1m/base.fvecs"
QUERY_FVECS = "/fast-lab-share/benchmarks/VectorDB/ANN/sift1m/query.fvecs"

# Output .bin paths (FIXED-POINT int32, Q16.16)
BASE_BIN_OUT = "/home/lifan3/cxl_dist_cal/data/base.bin"
QUERY_BIN_OUT = "/home/lifan3/cxl_dist_cal/data/query.bin"

# Expected vector dimension for SIFT1M
EXPECTED_DIM = 128

LIMIT_BASE = None   # e.g., 10000 converts only 10k base vectors
LIMIT_QUERY = None

# Fixed-point config: Q16.16 => scale = 2^16
FIXED_SCALE = 65536.0   # 2**16
# Optional clipping before scaling (None = no clipping)
CLIP_ABS = None         # e.g., 4.0 to clip to [-4, 4]

# =============================================================================
# CONVERSION + META LOGIC
# =============================================================================

def convert_fvecs_to_fixed_bin(in_path, out_path, expected_dim, limit=None):
    vec_count = 0
    total_bytes = 0
    max_abs_fixed = 0

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

            # Interpret as float32
            floats = np.frombuffer(data_bytes, dtype="<f4")  # little-endian float32

            # Optional clipping
            if CLIP_ABS is not None:
                floats = np.clip(floats, -CLIP_ABS, CLIP_ABS)

            # Convert to fixed-point Q16.16 (int32)
            fixed = np.round(floats * FIXED_SCALE).astype("<i4")  # little-endian int32

            # Track dynamic range
            if fixed.size > 0:
                max_abs_fixed = max(max_abs_fixed, int(np.max(np.abs(fixed))))

            # Write as raw int32; still dim * 4 bytes per vector
            out_bytes = fixed.tobytes()
            f_out.write(out_bytes)

            vec_count += 1
            total_bytes += len(out_bytes)

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
        meta.write(f"fixed_scale={FIXED_SCALE}\n")
        meta.write("fixed_format=Q16.16 (signed int32)\n")
        meta.write(f"max_abs_fixed={max_abs_fixed}\n")
        if CLIP_ABS is not None:
            meta.write(f"clip_abs={CLIP_ABS}\n")

    print(f"✅ Done: {vec_count} vectors written to {out_path}")
    print(f"   Meta info: {meta_path}")
    print(f"   Total size: {total_bytes / (1024**2):.2f} MB")
    print(f"   Max |value| in fixed-point: {max_abs_fixed}")

    return vec_count, total_bytes


def main():
    print("Converting base.fvecs → base.bin (fixed-point Q16.16) ...")
    convert_fvecs_to_fixed_bin(BASE_FVECS, BASE_BIN_OUT, EXPECTED_DIM, LIMIT_BASE)

    print("\nConverting query.fvecs → query.bin (fixed-point Q16.16) ...")
    convert_fvecs_to_fixed_bin(QUERY_FVECS, QUERY_BIN_OUT, EXPECTED_DIM, LIMIT_QUERY)


if __name__ == "__main__":
    main()
