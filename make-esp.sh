#!/bin/bash

# Create bootable ESP image for ARM64 UEFI testing
# Usage: ./make-esp.sh

set -e

# Configuration
BUILD_DIR="./output-MinGW-arm64"
WORK_DIR="$BUILD_DIR/qemu_freeldr_test"
UEFI_LOADER="${BUILD_DIR}/boot/freeldr/freeldr/uefildr.efi"
FREELDR_INI="./boot/freeldr/FREELDR.INI"
ESP_IMG="$WORK_DIR/esp.img"
ESP_SIZE=64  # MB

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

# Build the bootloader first
echo -e "${GREEN}Building ARM64 UEFI bootloader...${NC}"
cd "$BUILD_DIR"
if ninja uefildr; then
    echo -e "${GREEN}Build successful!${NC}"
else
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi
cd - > /dev/null

echo -e "${GREEN}Creating ARM64 UEFI ESP image...${NC}"

# Check if UEFI loader exists
if [ ! -f "$UEFI_LOADER" ]; then
    echo -e "${RED}Error: UEFI loader not found at $UEFI_LOADER${NC}"
    exit 1
fi

# Create directory structure
echo "Setting up ESP structure..."
rm -rf $WORK_DIR/esp
mkdir -p $WORK_DIR/esp/EFI/BOOT

# Copy UEFI loader to standard ARM64 boot location
echo "Copying UEFI loader..."
cp "$UEFI_LOADER" $WORK_DIR/esp/EFI/BOOT/BOOTAA64.EFI

# Copy freeldr.ini if it exists
if [ -f "$FREELDR_INI" ]; then
    echo "Copying freeldr.ini..."
    cp "$FREELDR_INI" $WORK_DIR/esp/freeldr.ini
else
    echo "Creating default freeldr.ini..."
    cat > $WORK_DIR/esp/freeldr.ini << 'EOF'
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
fi

# Create ESP disk image
echo "Creating ${ESP_SIZE}MB FAT32 ESP image..."
dd if=/dev/zero of="$ESP_IMG" bs=1M count=$ESP_SIZE status=none
mkfs.vfat -F 32 -n "SYSTEM" "$ESP_IMG" >/dev/null

# Copy files to image
if command -v mcopy &> /dev/null; then
    echo "Using mtools to copy files..."
    mmd -i "$ESP_IMG" ::/EFI 2>/dev/null || true
    mmd -i "$ESP_IMG" ::/EFI/BOOT 2>/dev/null || true
    mcopy -i "$ESP_IMG" $WORK_DIR/esp/EFI/BOOT/BOOTAA64.EFI ::/EFI/BOOT/
    mcopy -i "$ESP_IMG" $WORK_DIR/esp/freeldr.ini ::/

    echo -e "\n${GREEN}ESP image created successfully!${NC}"
    echo "Contents:"
    mdir -i "$ESP_IMG" ::/EFI/BOOT/
else
    echo "mtools not found. Using mount (requires sudo)..."
    sudo mkdir -p /mnt/esp
    sudo mount -o loop "$ESP_IMG" /mnt/esp
    sudo cp -r $WORK_DIR/esp/* /mnt/esp/
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
echo "    -serial stdio"