# TDX and Guest Images

## What is Intel TDX?

Intel TDX (Trusted Domain Extensions) is a hardware technology for running **Confidential Virtual Machines (CVMs)**:

- **Memory encryption:** Guest VM memory is encrypted, the host cannot read it
- **Attestation:** CPU cryptographically signs proof of the VM's code and state
- **Isolation:** Host OS and hypervisor are untrusted

Think of it as running a VM where even the server owner cannot see what's happening inside.

### How It Works

The core idea: hardware-enforced isolation + cryptographic proof.

1. CPU measures (hashes) all code loaded into the VM
2. CPU generates a "quote" (attestation) proving what code is running
3. Quote is signed by the CPU's private key (never leaves hardware)
4. Remote party verifies the quote using Intel's public certificates
5. If the quote matches expected values, we know exactly what code is running

This solves the problem of running untrusted code on someone else's server.

## Measurement Registers

When a TDX VM boots, Intel hardware measures everything loaded into it:

### MRTD (Measurement Register for TD)
- Hash of firmware code (TDVF - TDX Virtual Firmware)
- Hash of initial page tables
- Set during VM construction, never changes after boot

### RTMR[0-3] (Runtime Measurement Registers)
- **RTMR[0]:** Firmware data (configuration tables, ACPI, etc.)
- **RTMR[1]:** Linux kernel hash (including EFI stub)
- **RTMR[2]:** Initrd hash + kernel boot parameters
- **RTMR[3]:** Available for runtime use (COCOON uses this for config hash)

**Key property:** If MRTD and RTMRs match expected values → VM started from a known, verified state.

## COCOON Image System

### Image Structure

COCOON workers run from reproducible, verifiable images:

```
Image Components:
├── OVMF.fd        - TDX-enabled UEFI firmware (→ MRTD)
├── vmlinuz        - Linux kernel (→ RTMR[1])
├── initrd         - Initial ramdisk (→ RTMR[2])
├── cmdline        - Kernel boot parameters, includes rootfs hash (→ RTMR[2])
└── rootfs.img     - Root filesystem (read-only, dm-verity protected)
```

**Goals:**
- **Reproducible:** Same source → same image hash
- **Verifiable:** Anyone can rebuild and verify
- **Immutable:** Rootfs cannot be modified (dm-verity)
- **Flexible:** Runtime config provided separately

### Image Generation

We use **mkosi** with **Debian** for reproducible builds. The build process is defined in [`reprodebian/mkosi.conf`](../reprodebian/mkosi.conf) and related configuration files.

To build: run `mkosi build` in the `reprodebian/` directory. This generates all image components (firmware, kernel, initrd, rootfs) with deterministic hashes.

**Why mkosi + Debian?**
- **mkosi:** Modern tool for building reproducible OS images, supports TDX/confidential VMs
- **Debian:** Strong reproducible builds infrastructure via reproducible-builds.org
- Deterministic package selection and build process
- Good driver support (NVIDIA drivers available in Debian repositories)

## Boot Sequence

### 1. QEMU Initialization

QEMU starts the TDX VM with the following key components (see [`scripts/cocoon-launch`](../scripts/cocoon-launch) for full setup):

- **Machine type:** q35 with TDX confidential guest support enabled
- **Firmware (BIOS):** OVMF.fd with TDX support → measured into MRTD
- **Kernel:** vmlinuz → measured into RTMR[1]
- **Initrd and command line:** → both measured into RTMR[2]
- **Config volume:** Mounted via virtfs (9p) for guest to access
- **GPU:** Passed through via VFIO (if configured)

The TDX module automatically measures firmware, kernel, initrd, and boot parameters during VM construction.

### 2. Firmware (OVMF/TDVF)

1. TDX module measures firmware code → **MRTD**
2. TDVF measures kernel → **RTMR[1]**
3. EFI stub measures initrd + cmdline → **RTMR[2]**
4. Boot kernel with initrd

### 3. Initrd Phase

The initrd script mounts the verified rootfs using **dm-verity**:

1. Extracts the rootfs hash from kernel command line (measured in RTMR[2])
2. Sets up dm-verity device with that hash
3. Mounts the verified rootfs as read-only
4. Switches root to the verified filesystem

**dm-verity:** Verifies integrity on every read. Any tampering with rootfs will cause read failures when that portion is accessed.

The initrd is built by mkosi and includes minimal utilities needed for this setup.

### 4. COCOON Initialization (`cocoon-init`)

The initialization script ([`reprodebian/cocoon-init/cocoon-init`](../reprodebian/cocoon-init/cocoon-init)) runs as the first service and performs the following steps:

1. **Mount config volume** from host via virtfs (9p filesystem)
2. **Copy static config** to `/spec/`, excluding runtime directory
3. **Calculate config hash** using git-style hashing, respecting `.gitignore`
4. **Extend RTMR[3]** with the config hash (permanently records config in measurements)
5. **Derive persistent key** via `seal-client` (depends on MRTD + all RTMRs including config)
6. **Mark initialization complete** by extending RTMR[3] with "cocoon:tdx_inited" event
7. **Generate TLS certificate** with TDX attestation using `gen-cert`.
8. **Start application services** by executing the startup script from config

After this initialization:
- Base image is verified (measured in MRTD, RTMR[0-2])
- Config is verified (measured in RTMR[3])
- TLS certificate contains proof of exact VM state
- Persistent key is available for encrypted storage
- VM is ready to serve requests

## Config System

### Static vs Runtime Config

**Static Config** (measured into RTMR[3]):
```
/spec/
├── worker-config.json    # Worker settings
├── init-engines.sh       # Startup script
├── .gitignore           # Files to exclude from hash
└── runtime/             # Not measured (see below)
```

Config hash is part of TDX measurements → different config = different attestation.

**Runtime Config** (not measured):
```
/spec/runtime/
├── runtime.vars          # Runtime variables for config templates
└── global.config.json    # TON config with liteserver endpoints
```

Not part of measurements, but cannot affect security or correctness.

**Why separate?**
- Runtime parameters change frequently (e.g., liteserver endpoints)
- Correctness of computation is not affected
- Templates are verified (in static config), only values vary
- Examples: TON liteserver IPs, model download URLs, cache directories

## TDX Attestation (Quotes)

### Quote Structure

A TDX quote is a cryptographic proof generated by the CPU containing:

- **MRTD** - Firmware measurement
- **RTMR[0..3]** - Runtime measurements (RTMR[3] includes config hash)
- **REPORT_DATA** - 64 bytes of custom data (COCOON puts the hash of the TLS public key here)
- **SIGNATURE** - Signed by the CPU's attestation key

The quote proves "This specific code (identified by MRTD/RTMRs) generated this report data, and I (the CPU) vouch for it."

### Quote Generation

Inside the TDX guest, the VM can request the CPU to generate an attestation quote. The quote includes custom report data (64 bytes) - COCOON uses this to include the hash of the TLS certificate's public key.

See [RA-TLS documentation](ra-tls.md) for how quotes are embedded in TLS certificates.

### Quote Verification

Quote verification uses Intel's DCAP libraries to verify the CPU signature and extract measurements (MRTD, RTMRs, report data).

**Collaterals and PCCS:**

Verification requires "collaterals" (certificates, CRLs, TCB info) from Intel. Intel's rules require using PCCS (Provisioning Certificate Caching Service) to fetch these.

**Two key points:**
1. **Must use PCCS** - Intel requires collaterals to be fetched via PCCS (local cache), not directly
2. **Untrusted PCCS is fine** - We embed Intel's root key in our code, so PCCS cannot forge attestations (only cache and serve collaterals)

PCCS can run anywhere (host, separate server, or inside TDX). Multiple nodes can share one PCCS instance.

## Persistent Storage

Workers use encrypted storage for persistent data (wallet keys, state, etc.):

1. Persistent key is derived during initialization (see [Seal Keys](seal-keys.md))
2. Key is used to create/unlock a LUKS-encrypted volume
3. Encrypted volume is mounted at `/data`

**Key derivation:** The persistent key depends on the TDX measurements (MRTD + RTMRs), making it unique to the specific image and config combination.

**Security properties:**
- **Config isolation:** Different config → different key (malicious config can't access good config's data)
- **Host cannot decrypt:** Key only exists in TDX memory, the encrypted volume is opaque to the host
- **Hardware-bound:** Key cannot be exported or backed up (tied to CPU)
- **Rollback possible:** Host can provide old encrypted volume snapshot (limitation)

## Security Properties

TDX provides hardware-enforced isolation and cryptographic proof of what code is running. Key properties:

- **Measured boot:** Every component (firmware, kernel, initrd, config) is hashed into MRTD/RTMRs
- **Immutable rootfs:** dm-verity ensures rootfs integrity (read-time verification)
- **Config isolation:** Different configs get different keys (via RTMR[3])
- **Verifiable:** Anyone can rebuild the image and verify that measurements match

**Limitations:**
- Host can provide old snapshots (rollback attacks)
- Runtime directory is not measured (only use it for non-security-critical data)
- We trust Intel TDX hardware, Intel's attestation infrastructure, and Debian build tools

## Next Steps

- For RA-TLS (using quotes in TLS): [RA-TLS](ra-tls.md)
- For persistent keys: [Seal Keys](seal-keys.md)
- For deployment: [Deployment](deployment.md)