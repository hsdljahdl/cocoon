# GPU Confidential Computing

## Overview

COCOON workers use GPUs for AI inference inside TDX VMs. This requires:

1. **GPU Passthrough** - Gives the TDX guest direct access to GPU hardware (via VFIO)
2. **GPU Attestation** - Proves the GPU is in Confidential Computing mode
3. **Model Validation** - Verifies that the correct model is loaded

## Hardware Requirements

### Supported GPUs

**Minimum:**
- NVIDIA H100 or newer
- Confidential Computing (CC) support
- Updated VBIOS for attestation (requires NVIDIA support)

**Tested:**
- NVIDIA H200 NVL

**Note:** Consumer GPUs (RTX series) are not supported for production.

### VBIOS Update

**Required for GPU attestation** (proving GPU is in CC mode):

GPU attestation requires a CC-enabled VBIOS. Standard VBIOS versions may not support attestation features.

**How to update:**
Request a CC-enabled VBIOS from NVIDIA support, provide details and flash it per the instructions.

**Important:** VBIOS updates are only available through official NVIDIA support channels. This is not optional for production deployment.

## GPU Passthrough (VFIO)

### Concept

VFIO (Virtual Function I/O) allows TDX guest to access GPU hardware directly:
- Guest has direct GPU access (no hypervisor mediation)
- Near-native performance
- GPU memory isolated from host

### Host Setup

Use [`scripts/setup-gpu-vfio`](../scripts/setup-gpu-vfio) script:

```bash
# List GPUs
./scripts/setup-gpu-vfio

# Setup GPU for TDX passthrough
sudo ./scripts/setup-gpu-vfio --setup 0000:01:00.0

# Restore GPU to normal usage
sudo ./scripts/setup-gpu-vfio --restore 0000:01:00.0
```

The script:
1. Enables CC mode on GPU (disables pPCIe mode)
2. Detaches GPU from NVIDIA driver
3. Binds to vfio-pci driver
4. Configures udev rules for permissions (?)

**Manual setup:** The script is essentially a wrapper over [NVIDIA GPU admin tools](https://github.com/nvidia/gpu-admin-tool). It may better suit your needs as the script is not tested for different setups.

### QEMU Configuration

See [`scripts/cocoon-launch`](../scripts/cocoon-launch) for full QEMU setup.

GPU passed to guest via: `-device vfio-pci,host=<pci-address>`

### TDX Limitations
- Currently only one GPU per guest is supported. This restriction will be lifted in the future.

## GPU Attestation

### Basics

NVIDIA GPUs with CC support can generate hardware attestations similar to TDX:

1. GPU has hardware attestation key (burned into GPU)
2. GPU generates attestation including:
   - GPU model and VBIOS version
   - CC mode status (on/off)
   - Firmware measurements
3. Attestation signed by NVIDIA's root keys

### Verification at Boot

Implementation: [`reprodebian/gpu_attest/gpu_attest.py`](../reprodebian/gpu_attest/gpu_attest.py)

During TDX guest boot (via systemd service `nvidia-tdx.service`, runs before `cocoon-init`):

1. Script calls NVIDIA attestation SDK
2. Generates GPU evidence with random nonce
3. Verifies attestation against policy
4. Checks: CC mode enabled, firmware signatures valid, certificates not revoked

If verification fails → guest boot fails (prevents running without valid GPU).

**Policy:** See [`reprodebian/gpu_attest/attest_policy.json`](../reprodebian/gpu_attest/attest_policy.json)

Checks include:
- GPU architecture matches
- Attestation signature verified
- Certificate chains valid (not revoked)
- VBIOS and driver measurements match
- Nonce matches (prevents replay)

### Current Limitation

GPU attestation currently requires updated VBIOS. If VBIOS is outdated:
- Verification fails
- Guest won't start
- Contact NVIDIA support for VBIOS update

### Integration with COCOON

GPU verification happens at boot, result not currently included in TLS certificate user claims.

**Security note:** Malicious host could:
- Disable GPU (guest detects this and fails to start)
- Cannot affect correctness of successful computations
- Cannot fake GPU attestation (NVIDIA signature verification)

## Model Validation

Client needs to verify that the worker uses the correct model (not a cheaper/different one). Models are too large to include in TDX measurements.

**Solution:**

Models are built using [`scripts/build-model`](../scripts/build-model):

```bash
# Build model with automatic hash
./scripts/build-model google/gemma-3-270m

# Build specific commit
./scripts/build-model google/gemma-3-270m@abc123

# Verify against known hash
./scripts/build-model google/gemma-3-270m@abc123:def456...
```

**Build process:**
1. Downloads model from HuggingFace
2. Creates reproducible tar archive (sorted, normalized timestamps)
3. Generates dm-verity hash
4. Outputs: `model@commit:hash /path/to/tar`

**Caching and concurrency:**
- Skips tar creation if `.tar` exists
- Skips verity generation if `.hash` exists
- Per-model locking allows concurrent builds of different models
- Atomic writes prevent corruption on Ctrl-C

**How a worker proves the correct model:**

1. **dm-verity protection:** Model disk protected with dm-verity (host cannot tamper)
2. **Hash verification:** At QEMU startup, kernel verifies dm-verity hash
3. **Runtime vars:** MODEL_COMMIT and MODEL_VERITY_HASH passed to worker config

**Client verification:**
- Worker advertises model name with verity hash
- dm-verity ensures exact model content matches hash
- Host cannot tamper with model data without detection

**Benefit:** Single model archive can be shared by multiple workers on same host (read-only mount).

## Multi-GPU Setup

### Current Approach: Multiple Workers

Run separate TDX guests, one GPU each:

```bash
# Worker 1 - GPU 0, instance 0
./scripts/cocoon-launch --instance 0 --gpu 0000:01:00.0 worker.conf

# Worker 2 - GPU 1, instance 1
./scripts/cocoon-launch --instance 1 --gpu 0000:41:00.0 worker.conf
```

Each instance is independent (separate VM, separate wallet, separate keys).

### Future: Multiple GPUs per Worker

**Experimental** - a single worker with multiple GPUs may have issues, such as:
- Interrupt routing problems
- Poor parallelization in some models

**TDX 2.0:** Will improve multi-GPU support and add NVLink encryption.

## Security Considerations

### What GPU Attestation Proves

- Specific GPU model (H200, not H100 or A100)
- CC mode enabled (confidential computing active)
- VBIOS and driver versions (firmware integrity)
- NVIDIA signatures valid (not tampered)

### What GPU Attestation Does Not Prove

- GPU cannot be disabled by host (availability not guaranteed)
- Absence of side channels (timing, power analysis)
- Model integrity (handled separately via config hash)

### Trust Model

**We trust:**
- NVIDIA GPU hardware
- NVIDIA attestation infrastructure (signing keys, RIM service)
- GPU firmware (verified via attestation)

**We do NOT trust:**
- Host OS
- GPU could be unplugged (but computation correctness unaffected)

## Next Steps

- For TDX attestation: [TDX and Images](tdx-and-images.md)
- For RA-TLS integration: [RA-TLS](ra-tls.md)
- For deployment: [Deployment](deployment.md)