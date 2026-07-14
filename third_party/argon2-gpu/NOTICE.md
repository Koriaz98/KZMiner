# Third-party code notice

The CUDA Argon2 backend in this directory (`lib/argon2-cuda/`,
`lib/argon2-gpu-common/`, `include/argon2-cuda/`,
`include/argon2-gpu-common/`) is derived from:

**argon2-gpu**
Copyright (C) 2015-2017, Ondrej Mosnacek <omosnacek@gmail.com>
Original repository: https://gitlab.com/omos/argon2-gpu
WebDollar fork used as source: https://github.com/WebDollar/argon2-gpu

Licensed under the GNU General Public License v2 or later (GPLv2+).
See /LICENSE at the root of this repository for the full license text.

## Modifications made for KZMiner

- Target CUDA architectures updated to cover Turing, Ampere, Ada Lovelace,
  and Blackwell GPUs (`sm_75`, `sm_86`, `sm_89`, `sm_120`), replacing the
  original `sm_30` target.
- Fixed a device-selection ordering bug: the original code constructed
  CUDA resources (streams/events) via member initializer lists before
  calling `cudaSetDevice()` in the constructor body, causing
  `invalid resource handle` errors on any GPU other than device 0 in
  multi-GPU setups. KZMiner's calling code (`src/gpu/gpu_miner.cpp`)
  now explicitly calls `cudaSetDevice()` before constructing any CUDA
  object, working around this issue without modifying the vendored
  library itself.
- Vendored only the CUDA backend and its common dependencies; the
  OpenCL backend, benchmark tool, and test tool from the upstream
  project are not included.

This project (KZMiner) as a whole is licensed under GPLv2+ to comply
with the terms of this vendored dependency. See /LICENSE.
