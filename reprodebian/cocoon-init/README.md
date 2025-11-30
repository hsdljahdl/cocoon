# prepare-spec.sh

Creates a deterministic copy of a source directory with SHA-256 content hash.

## Usage

```bash
./prepare-spec.sh SRC_DIR SPEC_OUT
```

## What it does

1. **Copies files** from `SRC_DIR` to `SPEC_OUT`, respecting `.gitignore` rules
2. **Excludes** `.git` directories, `runtime/` folder, and spec metadata files
3. **Normalizes permissions** to git standard (644/755)
4. **Calculates SHA-256 hash** of the copied content
5. **Copies `runtime/` as-is** (not part of hash)
6. **Creates metadata** files: `spec.tree` (hash) and `spec.meta` (info)

## Output structure

```
SPEC_OUT/
├── app.js           # Copied files (normalized permissions)
├── config.json
├── spec.tree        # SHA-256 hash of content
├── spec.meta        # Metadata (timestamp, source path, etc.)
└── runtime/         # Copied as-is (not hashed)
    └── secrets.env
```

## Features

- **Deterministic**: Same input always produces same hash
- **Cross-platform**: Works on macOS and Linux
- **Read-only source**: Never modifies input directory
- **Hash verification**: Fails if existing `spec.tree` doesn't match
- **Debug mode**: Set `DEBUG=1` for verbose output
- **Dirty repo friendly**: Works with uncommitted changes and non-git directories

## Requirements

- `bash`, `git`, `find`, `tar`, `awk`
- Source directory must not contain nested `.git` directories
- Files must be ≤10MB (configurable via `MAX_FILE_SIZE`)
