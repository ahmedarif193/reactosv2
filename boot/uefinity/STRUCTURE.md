# Uefinity Project Structure

## Overview
Uefinity is a clean, multi-architecture UEFI bootloader extracted and refactored from ReactOS FreeLoader, maintained by Ahmed ARIF (Arif193@gmail.com).

## Current Status

### ✅ Completed
- Project directory structure created
- ARM64 files extracted from FreeLoader
- UEFI implementations copied (no BIOS code)
- Architecture-independent libraries extracted (filesystems, PE loader, INI parser)
- Architecture interface contract defined
- Standalone CMake build system created
- Multi-architecture support framework established
- Documentation and licensing in place

### 🚧 Pending Work
- Remove MachXXX abstraction layer (replace with direct UEFI calls)
- Clean x86/BIOS references and #ifdef blocks
- Implement full ARM64 arch interface
- Add Uefinity copyright headers to all files
- Test ARM64 build configuration
- Complete AMD64 migration from mixed BIOS/UEFI to pure UEFI

## Directory Contents

### `/arch/` - Architecture-specific implementations
- **arm64/** - Complete ARM64 implementation (10 files)
  - Entry point, MMU, GIC, timer, exceptions
  - Currently uses MachVtbl (needs refactoring)
- **amd64/** - Placeholder for future AMD64 support
- **riscv64/** - Placeholder for future RISC-V support

### `/common/` - Shared code across architectures
- **boot/** - Boot manager, OS selection (3 files)
- **uefi/** - UEFI service implementations (14 files)
- **fs/** - Filesystem drivers (6 files: FAT, NTFS, ext, Btrfs, ISO)
- **lib/** - Core libraries (7 files: PE loader, memory, INI parser)
- **ntldr/** - Windows/ReactOS loader (3 files)

### `/include/` - Header files
- **arch/** - Architecture interface and headers
- **uefi/** - UEFI protocol definitions

### `/docs/` - Documentation
- PORTING.md - Guide for adding new architectures

## Key Differences from FreeLoader

1. **No BIOS Support** - Pure UEFI implementation only
2. **No x86 Real Mode** - No legacy 16-bit code
3. **Clean Architecture Separation** - Each arch in its own directory
4. **Direct UEFI Calls** - No MachXXX abstraction (after refactoring)
5. **Standalone Build** - Independent CMake, not tied to ReactOS
6. **Multi-arch Design** - Clear interface for adding architectures

## File Statistics

- **Total Files**: 47 source files
- **ARM64 specific**: 11 files
- **UEFI services**: 14 files
- **Common libraries**: 20 files
- **Placeholder archs**: 2 files

## Build Instructions

```bash
# For ARM64
mkdir build-arm64
cd build-arm64
cmake .. -DARCH=arm64
make

# Output: uefinity.efi
```

## Next Steps for Full Implementation

1. **Refactor macharm64.c** to remove MachVtbl and use direct UEFI calls
2. **Update all common files** to remove x86 #ifdef blocks
3. **Implement ARM64 arch interface** fully
4. **Test on QEMU** with UEFI firmware
5. **Begin AMD64 migration** from FreeLoader

## License

GPL-2.0-or-later (inherited from ReactOS FreeLoader)