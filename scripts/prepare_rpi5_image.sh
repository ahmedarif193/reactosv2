#!/usr/bin/env bash
#
# prepare_rpi5_image.sh
# Prepare ReactOS ARM64 liveimg for Raspberry Pi 5
#
# This script:
# 1. Takes the generated liveimg.img
# 2. Downloads Raspberry Pi 5 EDK2 UEFI firmware if needed
# 3. Copies firmware to the ESP partition
# 4. Creates config.txt for RPi5 boot configuration
#
# Usage:
#   ./prepare_rpi5_image.sh [--input liveimg.img] [--output reactos-rpi5.img]
#

set -euo pipefail

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CACHE_DIR="$PROJECT_ROOT/.cache/rpi5-firmware"
DEFAULT_INPUT="$PROJECT_ROOT/output-Clang-arm64-Debug/liveimg.img"
DEFAULT_OUTPUT="$PROJECT_ROOT/output-Clang-arm64-Debug/reactos-rpi5.img"

# RPi5 EDK2 firmware URLs
# Using worproject/rpi5-uefi which provides proper RPi5 support
RPI_FIRMWARE_VERSION="v0.3"
RPI_FIRMWARE_REPO="worproject/rpi5-uefi"
RPI_FIRMWARE_ZIP="RPi5_UEFI_Release_${RPI_FIRMWARE_VERSION}.zip"
RPI_FIRMWARE_URL="https://github.com/${RPI_FIRMWARE_REPO}/releases/download/${RPI_FIRMWARE_VERSION}/${RPI_FIRMWARE_ZIP}"

# Files included in the firmware package:
#   - RPI_EFI.fd           : Main EDK2 UEFI firmware (goes in root)
#   - bcm2712-rpi-5-b.dtb  : Device tree for RPi5 (goes in root)
#   - config.txt           : Boot configuration (we'll create our own)

# Parse arguments
INPUT_IMG="$DEFAULT_INPUT"
OUTPUT_IMG="$DEFAULT_OUTPUT"
FORCE_DOWNLOAD=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        -i|--input)
            INPUT_IMG="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_IMG="$2"
            shift 2
            ;;
        -f|--force-download)
            FORCE_DOWNLOAD=1
            shift
            ;;
        -h|--help)
            sed -n '2,20p' "$0"
            exit 0
            ;;
        *)
            echo "Error: Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

# Helper functions
info() {
    echo "[rpi5] $*"
}

err() {
    echo "[rpi5] Error: $*" >&2
    exit 1
}

check_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        err "Required command not found: $1. Please install it and try again."
    fi
}

# Check required tools
check_command curl
check_command dd

# Detect mount/unmount commands (macOS vs Linux)
if [[ "$OSTYPE" == "darwin"* ]]; then
    MOUNT_CMD="hdiutil attach"
    UNMOUNT_CMD="hdiutil detach"
    MOUNT_NEEDS_SUDO=0
else
    MOUNT_CMD="mount"
    UNMOUNT_CMD="umount"
    MOUNT_NEEDS_SUDO=1
    check_command losetup
    check_command partprobe
fi

# Validate input
[[ -f "$INPUT_IMG" ]] || err "Input image not found: $INPUT_IMG"

info "Preparing Raspberry Pi 5 image from: $INPUT_IMG"
info "Output will be written to: $OUTPUT_IMG"

# Create cache directory
mkdir -p "$CACHE_DIR"

# Download and extract RPi firmware if needed
download_firmware() {
    local zip_file="$CACHE_DIR/$RPI_FIRMWARE_ZIP"
    local marker_file="$CACHE_DIR/.extracted_${RPI_FIRMWARE_VERSION}"

    # Check if already downloaded and extracted
    if [[ -f "$marker_file" ]] && [[ -f "$CACHE_DIR/RPI_EFI.fd" ]] && [[ $FORCE_DOWNLOAD -eq 0 ]]; then
        info "Firmware already cached (version: $RPI_FIRMWARE_VERSION)"
        return 0
    fi

    # Download the ZIP file
    if [[ ! -f "$zip_file" ]] || [[ $FORCE_DOWNLOAD -eq 1 ]]; then
        info "Downloading RPi firmware from: $RPI_FIRMWARE_URL"
        if ! curl -L -f -o "$zip_file.tmp" "$RPI_FIRMWARE_URL"; then
            err "Failed to download firmware from $RPI_FIRMWARE_URL"
        fi
        mv "$zip_file.tmp" "$zip_file"
        info "Downloaded: $RPI_FIRMWARE_ZIP"
    fi

    # Extract the ZIP file
    info "Extracting firmware package..."
    if ! unzip -q -o "$zip_file" -d "$CACHE_DIR"; then
        err "Failed to extract $zip_file"
    fi

    # Mark as extracted
    touch "$marker_file"
    info "Firmware extracted successfully (version: $RPI_FIRMWARE_VERSION)"
}

info "Checking RPi EDK2 UEFI firmware (version: $RPI_FIRMWARE_VERSION)"
check_command unzip
download_firmware

# Copy input image to output
info "Copying base image: $INPUT_IMG -> $OUTPUT_IMG"
cp -f "$INPUT_IMG" "$OUTPUT_IMG"

# Create a temporary mount point
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"; [[ -n "${LOOP_DEV:-}" ]] && sudo losetup -d "$LOOP_DEV" 2>/dev/null || true' EXIT

if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS: Use hdiutil to mount the ESP partition
    info "Mounting ESP partition (macOS)..."

    # Attach the disk image
    MOUNT_OUTPUT=$(hdiutil attach "$OUTPUT_IMG" -nomount 2>&1)
    DISK_DEV=$(echo "$MOUNT_OUTPUT" | grep -o '/dev/disk[0-9]*' | head -1)

    if [[ -z "$DISK_DEV" ]]; then
        err "Failed to attach disk image"
    fi

    info "Disk attached as: $DISK_DEV"

    # Find the EFI partition (usually partition 1)
    ESP_DEV="${DISK_DEV}s1"

    # Mount the partition
    MOUNT_POINT="$WORK_DIR/esp"
    mkdir -p "$MOUNT_POINT"

    if ! mount -t msdos "$ESP_DEV" "$MOUNT_POINT" 2>/dev/null; then
        err "Failed to mount ESP partition at $ESP_DEV"
    fi

    info "ESP partition mounted at: $MOUNT_POINT"

    # Cleanup function for macOS
    cleanup_mount() {
        info "Unmounting ESP partition..."
        umount "$MOUNT_POINT" 2>/dev/null || true
        hdiutil detach "$DISK_DEV" 2>/dev/null || true
    }
    trap 'cleanup_mount; rm -rf "$WORK_DIR"' EXIT

else
    # Linux: Use losetup and mount
    info "Mounting ESP partition (Linux)..."

    # Set up loop device
    LOOP_DEV=$(sudo losetup -f)
    sudo losetup -P "$LOOP_DEV" "$OUTPUT_IMG"
    sudo partprobe "$LOOP_DEV"

    # Mount the ESP partition (partition 1)
    ESP_DEV="${LOOP_DEV}p1"
    MOUNT_POINT="$WORK_DIR/esp"
    mkdir -p "$MOUNT_POINT"

    sudo mount "$ESP_DEV" "$MOUNT_POINT"
    info "ESP partition mounted at: $MOUNT_POINT"

    # Cleanup function for Linux
    cleanup_mount() {
        info "Unmounting ESP partition..."
        sudo umount "$MOUNT_POINT" 2>/dev/null || true
        sudo losetup -d "$LOOP_DEV" 2>/dev/null || true
    }
    trap 'cleanup_mount; rm -rf "$WORK_DIR"' EXIT
fi

# Copy RPi firmware files to ESP
info "Copying RPi firmware files to ESP..."

# Ensure EFI/BOOT directory exists (should already be there from ReactOS image)
if [[ "$OSTYPE" == "darwin"* ]]; then
    mkdir -p "$MOUNT_POINT/EFI/BOOT" 2>/dev/null || true
else
    sudo mkdir -p "$MOUNT_POINT/EFI/BOOT" 2>/dev/null || true
fi

# Copy the main UEFI firmware file to ROOT (not EFI/BOOT!)
# The RPi5 bootloader loads kernel=RPI_EFI.fd from root
RPI_EFI_SRC="$CACHE_DIR/RPI_EFI.fd"
RPI_EFI_DEST="$MOUNT_POINT/RPI_EFI.fd"

if [[ ! -f "$RPI_EFI_SRC" ]]; then
    err "RPI_EFI.fd not found in cache. Firmware extraction may have failed."
fi

info "Installing EDK2 UEFI firmware: RPI_EFI.fd -> /RPI_EFI.fd"
if [[ "$OSTYPE" == "darwin"* ]]; then
    cp "$RPI_EFI_SRC" "$RPI_EFI_DEST"
else
    sudo cp "$RPI_EFI_SRC" "$RPI_EFI_DEST"
fi

# Copy device tree file to ROOT (required for RPi5)
DTB_SRC="$CACHE_DIR/bcm2712-rpi-5-b.dtb"
DTB_DEST="$MOUNT_POINT/bcm2712-rpi-5-b.dtb"

if [[ -f "$DTB_SRC" ]]; then
    info "Copying device tree: bcm2712-rpi-5-b.dtb -> /bcm2712-rpi-5-b.dtb"
    if [[ "$OSTYPE" == "darwin"* ]]; then
        cp "$DTB_SRC" "$DTB_DEST"
    else
        sudo cp "$DTB_SRC" "$DTB_DEST"
    fi
else
    err "bcm2712-rpi-5-b.dtb not found in firmware package. This file is required for RPi5 boot."
fi

# Verify ReactOS bootloader exists (should already be in the image)
if [[ -f "$MOUNT_POINT/EFI/BOOT/bootaa64.efi" ]] || [[ -f "$MOUNT_POINT/efi/boot/bootaa64.efi" ]]; then
    info "ReactOS UEFI loader (bootaa64.efi) found - OK"
else
    info "Warning: bootaa64.efi not found in EFI/BOOT - ReactOS may not boot after UEFI"
fi

# Create custom config.txt for ReactOS
info "Creating config.txt for Raspberry Pi 5..."

CONFIG_TXT="$MOUNT_POINT/config.txt"

# CRITICAL: os_check=0 MUST be BEFORE the [pi5] section to skip OS compatibility check
# Without this, RPi5 bootloader will refuse to boot with "OS does not indicate support"
CONFIG_CONTENT='# Raspberry Pi 5 configuration for ReactOS ARM64
# Generated by prepare_rpi5_image.sh

# CRITICAL: Skip OS compatibility check (must be before [pi5] section)
os_check=0

[pi5]
# EDK2 UEFI firmware (loaded by RPi5 bootloader)
kernel=RPI_EFI.fd

# ARM64 mode
arm_64bit=1

# Memory (adjust based on your RPi5 model)
total_mem=4096

# HDMI/Display
hdmi_force_hotplug=1
hdmi_drive=2

# USB
max_usb_current=1

# Disable rainbow splash
disable_splash=1

# UART for debugging (GPIO 14/15, 115200 baud)
enable_uart=1
uart_2ndstage=1

# PCIe support
dtparam=pciex1

# Boot settings
boot_delay=1

# Boot flow:
# 1. RPi5 bootloader loads this config.txt
# 2. os_check=0 bypasses OS compatibility check
# 3. kernel=RPI_EFI.fd loads EDK2 UEFI firmware
# 4. EDK2 UEFI loads EFI/BOOT/bootaa64.efi (FreeLdr)
# 5. FreeLdr boots ReactOS
'

if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "$CONFIG_CONTENT" > "$CONFIG_TXT"
else
    echo "$CONFIG_CONTENT" | sudo tee "$CONFIG_TXT" > /dev/null
fi

info "config.txt created successfully"

# Create a README for RPi5 users
README="$MOUNT_POINT/REACTOS_RPI5_README.txt"
README_CONTENT='ReactOS ARM64 for Raspberry Pi 5
=================================

This image has been configured to boot on Raspberry Pi 5 with EDK2 UEFI firmware.

Boot Flow:
1. RPi5 bootloader reads config.txt (os_check=0 bypasses OS check)
2. Loads RPI_EFI.fd (EDK2 UEFI firmware)
3. EDK2 UEFI loads EFI/BOOT/bootaa64.efi (FreeLdr)
4. FreeLdr boots ReactOS

Installation Instructions:
1. Write this image to a microSD card:
   - macOS:   sudo dd if=reactos-rpi5.img of=/dev/rdiskN bs=1m
   - Linux:   sudo dd if=reactos-rpi5.img of=/dev/sdX bs=1M status=progress
   - Windows: Use Rufus or Win32DiskImager

2. Insert the microSD card into your Raspberry Pi 5

3. Connect:
   - HDMI display
   - USB keyboard and mouse
   - Power supply (5V 5A recommended)

4. Power on the Raspberry Pi 5

Required Files (in root of boot partition):
- config.txt           : Boot configuration with os_check=0
- RPI_EFI.fd           : EDK2 UEFI firmware
- bcm2712-rpi-5-b.dtb  : Device tree for RPi5

Troubleshooting:
- If boot fails with "OS does not indicate support", ensure os_check=0
  is present BEFORE the [pi5] section in config.txt
- Check UART output on GPIO pins 14/15 (115200 baud)
- Ensure bcm2712-rpi-5-b.dtb exists in the root directory
- Try a different microSD card if issues persist

Firmware Source:
- EDK2 UEFI: https://github.com/worproject/rpi5-uefi

For more information, visit:
https://github.com/AhmedSaid/reactos
'

if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "$README_CONTENT" > "$README"
else
    echo "$README_CONTENT" | sudo tee "$README" > /dev/null
fi

info "README created: REACTOS_RPI5_README.txt"

# List installed files for verification
info "Verifying installed files..."
if [[ "$OSTYPE" == "darwin"* ]]; then
    ls -la "$MOUNT_POINT/RPI_EFI.fd" "$MOUNT_POINT/bcm2712-rpi-5-b.dtb" "$MOUNT_POINT/config.txt" 2>/dev/null || true
else
    sudo ls -la "$MOUNT_POINT/RPI_EFI.fd" "$MOUNT_POINT/bcm2712-rpi-5-b.dtb" "$MOUNT_POINT/config.txt" 2>/dev/null || true
fi

# Sync and unmount
info "Syncing filesystem..."
sync

cleanup_mount

info "✓ Raspberry Pi 5 image prepared successfully!"
info "Output: $OUTPUT_IMG"
info ""
info "Next steps:"
info "  1. Write the image to a microSD card:"
if [[ "$OSTYPE" == "darwin"* ]]; then
    info "     sudo dd if=$OUTPUT_IMG of=/dev/rdiskN bs=1m"
else
    info "     sudo dd if=$OUTPUT_IMG of=/dev/sdX bs=1M status=progress"
fi
info "  2. Insert into Raspberry Pi 5 and power on"
info "  3. ReactOS should boot via EDK2 UEFI firmware"
info ""
info "For more details, see REACTOS_RPI5_README.txt on the boot partition"
