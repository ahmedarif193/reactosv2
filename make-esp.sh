#!/bin/bash

# Create bootable ESP image for ARM64 UEFI testing and run QEMU
# Usage: ./make-esp.sh

set -euo pipefail

# Runtime controls (override via environment):
#   QEMU_TIMEOUT  - seconds to run QEMU before killing it (default: 120)
#   QEMU_DISPLAY  - QEMU -display argument (default: gtk). Use 'none' for headless.
#   QEMU_SERIAL   - serial log path (default: /tmp/aarch64.log)
#   QEMU_EXTRA    - extra args appended to QEMU command
QEMU_TIMEOUT="${QEMU_TIMEOUT:-120}"
QEMU_DISPLAY="${QEMU_DISPLAY:-gtk}"
QEMU_SERIAL="${QEMU_SERIAL:-/tmp/aarch64.log}"
QEMU_EXTRA=${QEMU_EXTRA:-}

# Configuration
BUILD_DIR="./output-MinGW-arm64"
WORK_DIR="$BUILD_DIR/qemu_freeldr_test"
UEFI_LOADER="${BUILD_DIR}/boot/freeldr/freeldr/uefildr.efi"
# Use LiveCD configuration by default to match LiveCD boot menu
FREELDR_INI="./boot/bootdata/livecd.ini"
ESP_IMG="$WORK_DIR/esp.img"
ESP_SIZE=64  # MB

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${GREEN}Building ARM64 UEFI bootloader and rosload...${NC}"
pushd "$BUILD_DIR" >/dev/null
if ninja uefildr; then
    echo -e "${GREEN}uefildr build successful!${NC}"
else
    echo -e "${RED}uefildr build failed!${NC}"
    exit 1
fi
# rosload is not used on ARM64 UEFI; skip
popd >/dev/null

echo -e "${GREEN}Creating ARM64 UEFI ESP image...${NC}"

if [ ! -f "$UEFI_LOADER" ]; then
    echo -e "${RED}Error: UEFI loader not found at $UEFI_LOADER${NC}"
    exit 1
fi

echo "Setting up ESP structure..."
rm -rf "$WORK_DIR/esp"
mkdir -p "$WORK_DIR/esp/EFI/BOOT"

echo "Copying UEFI loader..."
cp "$UEFI_LOADER" "$WORK_DIR/esp/EFI/BOOT/BOOTAA64.EFI"

# Optional: copy rosload if present
ROSLOAD="${BUILD_DIR}/boot/freeldr/freeldr/rosload.exe"
if [ -f "$ROSLOAD" ]; then
    echo "Copying rosload.exe..."
    cp "$ROSLOAD" "$WORK_DIR/esp/rosload.exe"
else
    echo "rosload.exe not found, skipping..."
fi

# freeldr.ini
if [ -f "$FREELDR_INI" ]; then
    echo "Copying freeldr.ini..."
    cp "$FREELDR_INI" "$WORK_DIR/esp/freeldr.ini"
    # Also copy as boot.ini for compatibility
    cp "$FREELDR_INI" "$WORK_DIR/esp/boot.ini"
else
    echo "Creating default freeldr.ini..."
    cat > "$WORK_DIR/esp/freeldr.ini" << 'EOF'
[FREELOADER]
MessageBox=ReactOS ARM64 UEFI Loader\nPress any key to continue...
DefaultOS=TestMenu
TimeOut=5
Debug=/DEBUG /DEBUGPORT=SERIAL /BAUDRATE=115200

[ARM64]
EnableSerialDebug=Yes
SerialBaudRate=115200
EnableEfiChainload=Yes
UefiGopMode=0

[Display]
TitleText=ReactOS ARM64 UEFI FreeLDR
MenuTextColor=White
MenuColor=Blue
TextColor=Yellow
MinimalUI=Yes

[Operating Systems]
TestMenu="Test Boot Menu"
EfiShell="UEFI Shell"

[TestMenu]
BootType=Windows2003
SystemPath=multi(0)disk(0)rdisk(0)partition(1)\reactos
Options=/DEBUGPORT=SCREEN

[EfiShell]
BootType=EfiApplication
EfiAppPath=\EFI\BOOT\Shell.efi
Options=
EOF
    # Also create boot.ini as a copy
    cp "$WORK_DIR/esp/freeldr.ini" "$WORK_DIR/esp/boot.ini"
fi

echo "Creating ${ESP_SIZE}MB FAT32 ESP image..."
mkdir -p "$WORK_DIR"
dd if=/dev/zero of="$ESP_IMG" bs=1M count=$ESP_SIZE status=none
mkfs.vfat -F 32 -n "SYSTEM" "$ESP_IMG" >/dev/null

if command -v mcopy &> /dev/null; then
    echo "Using mtools to copy files..."
    mmd -i "$ESP_IMG" ::/EFI 2>/dev/null || true
    mmd -i "$ESP_IMG" ::/EFI/BOOT 2>/dev/null || true
    mcopy -i "$ESP_IMG" "$WORK_DIR/esp/EFI/BOOT/BOOTAA64.EFI" ::/EFI/BOOT/
    mcopy -i "$ESP_IMG" "$WORK_DIR/esp/freeldr.ini" ::/
    # Also copy boot.ini if it exists
    if [ -f "$WORK_DIR/esp/boot.ini" ]; then
        mcopy -i "$ESP_IMG" "$WORK_DIR/esp/boot.ini" ::/
    fi
    if [ -f "$WORK_DIR/esp/rosload.exe" ]; then
        mcopy -i "$ESP_IMG" "$WORK_DIR/esp/rosload.exe" ::/
    fi
    echo -e "\n${GREEN}ESP image created successfully!${NC}"
    echo "Contents:"
    mdir -i "$ESP_IMG" ::/ 2>/dev/null || true
    mdir -i "$ESP_IMG" ::/EFI/BOOT/
else
    echo "mtools not found. Using mount (requires sudo)..."
    sudo mkdir -p /mnt/esp
    sudo mount -o loop "$ESP_IMG" /mnt/esp
    sudo cp -r "$WORK_DIR/esp"/* /mnt/esp/
    sudo umount /mnt/esp
    echo -e "\n${GREEN}ESP image created successfully!${NC}"
fi

echo -e "\nImage location: ${GREEN}$ESP_IMG${NC}"
echo -e "\nTo test with QEMU, run:"
echo "qemu-system-aarch64 \\" 
echo "    -M virt \\" 
echo "    -cpu cortex-a72 \\" 
echo "    -m 2G \\" 
echo "    -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \\" 
echo "    -drive file=$ESP_IMG,format=raw,if=none,id=boot \\" 
echo "    -device virtio-blk-pci,drive=boot,bootindex=0 \\" 
echo "    -device qemu-xhci \\" 
echo "    -device usb-kbd \\" 
echo "    -device usb-tablet \\" 
echo "    -device ramfb \\" 
echo "    -display $QEMU_DISPLAY \\" 
echo "    -serial file:$QEMU_SERIAL" 

# Run QEMU with internal timeout so callers don't need an outer timeout
if command -v timeout >/dev/null 2>&1; then
  TIMEOUT_CMD=(timeout "${QEMU_TIMEOUT}")
else
  TIMEOUT_CMD=()
fi

"${TIMEOUT_CMD[@]}" qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a72 \
  -m 2G \
  -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
  -drive file=$ESP_IMG,format=raw,if=none,id=boot \
  -device virtio-blk-pci,drive=boot,bootindex=0 \
  -device qemu-xhci \
  -device usb-kbd \
  -device usb-tablet \
  -device ramfb \
  -display "$QEMU_DISPLAY" \
  -serial file:"$QEMU_SERIAL" \
  ${QEMU_EXTRA}

# Show the first 300 lines of the serial log if available
if [ -f "$QEMU_SERIAL" ]; then
  echo -e "\n--- $QEMU_SERIAL (first 300 lines) ---"
  sed -n '1,300p' "$QEMU_SERIAL"
fi

    # 145   -echo "qemu-system-aarch64 \\"
    # 146   -echo "    -M virt \\"
    # 147   -echo "    -cpu cortex-a72 \\"
    # 148   -echo "    -m 2G \\"
    # 149   -echo "    -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \\"
    # 150   -echo "    -drive file=$ESP_IMG,format=raw,if=none,id=boot \\"
    # 151   -echo "    -device virtio-blk-pci,drive=boot,bootindex=0 \\"
    # 152   -echo "    -device virtio-gpu-pci \\"
    # 153   -echo "    -display $QEMU_DISPLAY \\"
    # 154   -echo "    -serial file:$QEMU_SERIAL"

# qemu-system-aarch64 \
#   -M virt \
#   -cpu cortex-a72 \
#   -m 2G \
#   -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
#   -drive file=/home/ahmed/WorkDir/reactos_arm64/output-MinGW-arm64/qemu_freeldr_test/esp.img,format=raw,if=none,id=boot \
#   -device virtio-blk-pci,drive=boot,bootindex=0 \
#   -device virtio-gpu-pci \
#   -display gtk

# qemu-system-aarch64 \
#   -M virt \
#   -cpu cortex-a72 \
#   -m 2G \
#   -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
#   -drive file=/home/ahmed/WorkDir/reactos_arm64/output-MinGW-arm64/qemu_freeldr_test/esp.img,format=raw,if=none,id=boot \
#   -device virtio-blk-pci,drive=boot,bootindex=0 \
#   -device ramfb \
#   -display gtk
