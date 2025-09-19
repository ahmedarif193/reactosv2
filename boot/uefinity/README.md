# Uefinity - Multi-Architecture UEFI Bootloader

Uefinity is a modern, clean UEFI-only bootloader designed for multiple architectures with no legacy BIOS dependencies.

## Project Information

- **Maintainer**: Ahmed ARIF
- **Email**: Arif193@gmail.com
- **License**: GPL-2.0-or-later
- **Based on**: ReactOS FreeLoader (UEFI components only)

## Features

- Pure UEFI implementation - no BIOS legacy code
- Multi-architecture support (ARM64, AMD64, RISC-V64)
- Clean, modular architecture
- Direct UEFI service calls without abstraction layers
- Minimal code duplication between architectures
- Support for multiple filesystems (FAT, NTFS, ext2/3/4, Btrfs)
- PE/COFF and ELF loader support
- Windows and Linux boot support

## Architecture Support

| Architecture | Status | Description |
|--------------|--------|-------------|
| ARM64 | Active Development | Full UEFI support, initial implementation |
| AMD64 | Planned | Migration from mixed BIOS/UEFI to pure UEFI |
| RISC-V64 | Future | Placeholder for RISC-V UEFI support |

## Directory Structure

```
uefinity/
├── arch/           # Architecture-specific implementations
│   ├── arm64/      # ARM64-specific code
│   ├── amd64/      # AMD64-specific code
│   └── riscv64/    # RISC-V64-specific code
├── common/         # Shared code across architectures
│   ├── boot/       # Boot manager and OS selection
│   ├── uefi/       # UEFI service wrappers
│   ├── fs/         # Filesystem drivers
│   ├── lib/        # Common libraries
│   └── ntldr/      # NT/Windows loader components
├── include/        # Header files
│   ├── arch/       # Architecture-specific headers
│   └── uefi/       # UEFI protocol definitions
└── docs/           # Documentation
```

## Building Uefinity

### Prerequisites

- GCC cross-compiler for target architecture
- CMake 3.10 or later
- UEFI development headers

### Build Instructions

```bash
# Configure for ARM64
mkdir build-arm64
cd build-arm64
cmake .. -DARCH=arm64 -DCMAKE_TOOLCHAIN_FILE=../cmake/arm64.cmake

# Build
make

# Output: uefinity.efi
```

## Contributing

See [CONTRIBUTING.md](docs/CONTRIBUTING.md) for guidelines on contributing to Uefinity.

For architecture porting guide, see [PORTING.md](docs/PORTING.md).

## Acknowledgments

Uefinity is based on the UEFI components of ReactOS FreeLoader, with significant refactoring to remove legacy BIOS dependencies and create a clean, multi-architecture design.

## Contact

Maintained by Ahmed ARIF - Arif193@gmail.com