# Uefinity Dependency Management System

This document describes the dependency management system for the Uefinity bootloader project.

## Overview

The Uefinity project uses a Python-based dependency management system to automatically download, manage, and integrate third-party libraries. This system ensures consistent builds across different environments and simplifies the process of adding new dependencies.

## Directory Structure

```
boot/uefinity/
├── scripts/
│   └── manage_deps.py      # Main dependency management script
├── 3rdparty/               # Downloaded third-party libraries
│   ├── .stamps/            # Stamp files tracking checkout status
│   └── rapidyaml/          # Example: rapidyaml library
├── deps.yaml               # Dependency configuration file
└── CMakeLists.txt          # Build system with dependency integration
```

## Configuration File (deps.yaml)

The `deps.yaml` file defines all third-party dependencies. Each library entry supports:

- **repository**: Git repository URL (required)
- **branch**: Branch name to checkout (optional, default: master)
- **tag**: Tag name to checkout (optional, overrides branch)
- **commit**: Specific commit hash (optional, overrides branch/tag)
- **directory**: Target directory name in 3rdparty/ (optional, default: library name)

### Example Configuration

```yaml
thirdpartylibs:
  rapidyaml:
    repository: "https://github.com/biojppm/rapidyaml.git"
    branch: "master"
    directory: "rapidyaml"

  cjson:
    repository: "https://github.com/DaveGamble/cJSON.git"
    tag: "v1.7.16"
    directory: "cjson"

  specific_commit_example:
    repository: "https://github.com/example/library.git"
    commit: "abc123def456789012345678901234567890abcd"
    directory: "library"
```

## Usage

### Command Line

```bash
# Basic usage (uses deps.yaml in current directory)
python3 scripts/manage_deps.py

# Specify custom config file
python3 scripts/manage_deps.py --config /path/to/deps.yaml

# Force re-checkout of all dependencies
python3 scripts/manage_deps.py --force

# Specify different base directory
python3 scripts/manage_deps.py --base-dir /path/to/project
```

### CMake Integration

The dependency management is automatically integrated into the CMake build process:

1. CMake checks for Python and PyYAML
2. Runs dependency management script before build
3. Configures include paths for third-party libraries
4. Adds third-party source files to the build

### Manual Testing

To test the dependency management system independently:

```bash
cmake -P test_deps.cmake
```

## Features

### Automatic Dependency Management

- **Smart Checkout**: Only downloads/updates when necessary
- **Version Tracking**: Supports branches, tags, and specific commits
- **Submodule Support**: Automatically initializes git submodules
- **Stamp Files**: Tracks current checkout status to avoid unnecessary operations

### Robust Error Handling

- **Validation**: Checks for required tools and packages
- **Logging**: Comprehensive logging to file and console
- **Recovery**: Handles network issues and repository problems gracefully

### Build System Integration

- **CMake Integration**: Seamlessly integrates with existing build system
- **Include Paths**: Automatically configures header search paths
- **Source Files**: Adds third-party sources to build targets

## Stamp Files

The system creates stamp files (`.stamp_<library_name>`) to track the current state of each dependency:

```
# Example stamp file content
repository_url=https://github.com/biojppm/rapidyaml.git
checkout_ref=master
git_hash=833786111c5ec1e7fc5b8f10ac175362a8070b31
checkout_date=2025-09-19T19:01:16.821721
```

## Adding New Dependencies

1. Edit `deps.yaml` to add the new library
2. Update `CMakeLists.txt` to include necessary source files and headers
3. Run the dependency management script or rebuild with CMake

### Example: Adding a JSON library

```yaml
# In deps.yaml
thirdpartylibs:
  cjson:
    repository: "https://github.com/DaveGamble/cJSON.git"
    tag: "v1.7.16"
    directory: "cjson"
```

```cmake
# In CMakeLists.txt
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/cjson")
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/cjson")
    file(GLOB CJSON_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/cjson/*.c")
    list(APPEND THIRDPARTY_SOURCES ${CJSON_SOURCES})
endif()
```

## Requirements

- **Python 3.7+**: For running the dependency management script
- **PyYAML**: Python package for YAML parsing (`pip install PyYAML`)
- **Git**: For cloning and managing repositories
- **CMake 3.10+**: For build system integration

## Troubleshooting

### Common Issues

1. **PyYAML not found**: Install with `pip install PyYAML`
2. **Git authentication**: Ensure you have access to private repositories
3. **Network issues**: Script will retry and provide meaningful error messages
4. **Disk space**: Ensure adequate space for downloading dependencies

### Force Refresh

If dependencies become corrupted or you need to reset them:

```bash
python3 scripts/manage_deps.py --force
```

### Debug Information

Check the log file `dependency_manager.log` for detailed operation information.

## Security Considerations

- **Repository URLs**: Only use trusted repository sources
- **Commit Verification**: Consider using specific commit hashes for security-critical dependencies
- **Submodules**: Be aware that submodules may introduce additional dependencies

## Performance

- **Caching**: Stamp files prevent unnecessary re-downloads
- **Incremental Updates**: Only fetches changes when needed
- **Parallel Processing**: Could be extended for parallel dependency processing

## Future Enhancements

- **Checksums**: Verify downloaded content integrity
- **Mirrors**: Support for repository mirrors and fallbacks
- **Dependency Resolution**: Handle inter-dependency relationships
- **Package Managers**: Integration with package managers like vcpkg or Conan