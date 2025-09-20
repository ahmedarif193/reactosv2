#!/bin/bash

# Create bootable ESP image for ARM64 FreeLoader testing
# Usage: ./make-esp.sh

set -euo pipefail

# Runtime controls (override via environment):
#   QEMU_TIMEOUT  - seconds to run QEMU before killing it (default: 120)
#   QEMU_DISPLAY  - QEMU -display argument (default: gtk). Use 'none' for headless.
#   QEMU_SERIAL   - serial log path (default: /tmp/freeldr_arm64.log)
#   QEMU_EXTRA    - extra args appended to QEMU command
QEMU_TIMEOUT="${QEMU_TIMEOUT:-120}"
QEMU_DISPLAY="${QEMU_DISPLAY:-gtk}"
QEMU_SERIAL="${QEMU_SERIAL:-/tmp/freeldr_arm64.log}"
QEMU_EXTRA=${QEMU_EXTRA:-}

# Configuration
BUILD_DIR="./output-MinGW-arm64"
WORK_DIR="$BUILD_DIR/qemu_freeldr_test"
UEFI_LOADER="${BUILD_DIR}/boot/freeldr/freeldr/uefildr.efi"
ESP_IMG="$WORK_DIR/freeldr.img"
ESP_SIZE=128  # MB (larger for ReactOS files)

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}Building FreeLoader ARM64 UEFI bootloader...${NC}"
pushd "$BUILD_DIR" >/dev/null
if ninja uefildr; then
    echo -e "${GREEN}uefildr.efi build successful!${NC}"
else
    echo -e "${RED}uefildr.efi build failed!${NC}"
    exit 1
fi
popd >/dev/null

echo -e "${GREEN}Creating FreeLoader ARM64 UEFI ESP image...${NC}"

if [ ! -f "$UEFI_LOADER" ]; then
    echo -e "${RED}Error: FreeLoader UEFI loader not found at $UEFI_LOADER${NC}"
    exit 1
fi

echo "Setting up ESP structure..."
rm -rf "$WORK_DIR/esp"
mkdir -p "$WORK_DIR/esp/EFI/BOOT"
mkdir -p "$WORK_DIR/esp/reactos/system32"
mkdir -p "$WORK_DIR/esp/reactos/system32/drivers"
mkdir -p "$WORK_DIR/esp/reactos/system32/config"
mkdir -p "$WORK_DIR/esp/freeldr"

echo "Copying FreeLoader UEFI loader..."
cp "$UEFI_LOADER" "$WORK_DIR/esp/EFI/BOOT/BOOTAA64.EFI"

echo "Copying ReactOS kernel and HAL..."
# Copy kernel
if [ -f "$BUILD_DIR/ntoskrnl/ntoskrnl.exe" ]; then
    echo "  - Copying ntoskrnl.exe..."
    cp "$BUILD_DIR/ntoskrnl/ntoskrnl.exe" "$WORK_DIR/esp/reactos/system32/"
else
    echo -e "${YELLOW}  - Warning: ntoskrnl.exe not found${NC}"
fi

# Copy HAL
if [ -f "$BUILD_DIR/hal/halarm64/hal.dll" ]; then
    echo "  - Copying hal.dll..."
    cp "$BUILD_DIR/hal/halarm64/hal.dll" "$WORK_DIR/esp/reactos/system32/"
else
    echo -e "${YELLOW}  - Warning: hal.dll not found${NC}"
fi

# Copy bootdata files if available
if [ -d "$BUILD_DIR/boot/bootdata" ]; then
    echo "Copying bootdata files..."
    # Copy registry hives if they exist
    for hive in system software default sam security; do
        if [ -f "$BUILD_DIR/boot/bootdata/$hive" ]; then
            echo "  - Copying $hive registry hive..."
            cp "$BUILD_DIR/boot/bootdata/$hive" "$WORK_DIR/esp/reactos/system32/config/"
        fi
    done

    # Copy hivesys.inf if it exists (system hive template)
    if [ -f "$BUILD_DIR/boot/bootdata/hivesys.inf" ]; then
        echo "  - Copying hivesys.inf..."
        cp "$BUILD_DIR/boot/bootdata/hivesys.inf" "$WORK_DIR/esp/reactos/system32/"
    fi
fi

# Copy some essential drivers if they exist
echo "Copying kernel-mode drivers..."
DRIVERS_PATH="$BUILD_DIR/drivers"
if [ -d "$DRIVERS_PATH" ]; then
    while IFS= read -r -d '' driver; do
        name=$(basename "$driver")
        echo "  - $name"
        cp "$driver" "$WORK_DIR/esp/reactos/system32/drivers/"
    done < <(find "$DRIVERS_PATH" -type f -name '*.sys' -print0)
else
    echo -e "${YELLOW}  - Warning: drivers directory not found${NC}"
fi

# Identify the boot-critical driver list from hivesys.inf and make sure we staged them
CRITICAL_DRIVER_LIST=$(python3 - <<'PY'
import re
from pathlib import Path

inf_path = Path('boot/bootdata/hivesys.inf')
critical = set()
pattern = re.compile(r'system32\\drivers\\([^"\\]+\.sys)', re.IGNORECASE)

with inf_path.open('r', encoding='utf-8', errors='ignore') as f:
    for line in f:
        match = pattern.search(line)
        if match:
            critical.add(match.group(1).lower())

print('\n'.join(sorted(critical)))
PY
)

if [ -n "$CRITICAL_DRIVER_LIST" ]; then
    echo "Ensuring boot-critical drivers from hivesys.inf are present..."
    while IFS= read -r driver_name; do
        [ -z "$driver_name" ] && continue
        dest="$WORK_DIR/esp/reactos/system32/drivers/${driver_name}"
        if [ -f "$dest" ]; then
            continue
        fi

        src=$(find "$BUILD_DIR" -type f -name "$driver_name" -print -quit)
        if [ -n "$src" ]; then
            echo "  - staging ${driver_name}"
            cp "$src" "$dest"
        else
            echo -e "${YELLOW}  - Warning: boot-critical driver ${driver_name} not found in build output${NC}"
        fi
    done <<< "$CRITICAL_DRIVER_LIST"
fi

# Copy bootvid if it exists (boot video driver)
if [ -f "$BUILD_DIR/drivers/base/bootvid/bootvid.dll" ]; then
    echo "  - Copying bootvid.dll..."
    cp "$BUILD_DIR/drivers/base/bootvid/bootvid.dll" "$WORK_DIR/esp/reactos/system32/"
fi

# Copy kdcom if it exists (kernel debugger communication)
if [ -f "$BUILD_DIR/drivers/base/kdcom/kdcom.dll" ]; then
    echo "  - Copying kdcom.dll..."
    cp "$BUILD_DIR/drivers/base/kdcom/kdcom.dll" "$WORK_DIR/esp/reactos/system32/"
fi

# Copy essential NLS data files required by the loader
echo "Copying NLS data files..."
NLS_FILES=(
    "c_1252.nls"
    "c_437.nls"
    "l_intl.nls"
    "locale.nls"
    "ctype.nls"
    "sortkey.nls"
    "sorttbls.nls"
)

for nls in "${NLS_FILES[@]}"; do
    FOUND=""
    for candidate in \
        "$BUILD_DIR/reactos/system32/$nls" \
        "$BUILD_DIR/media/nls/$nls" \
        "./media/nls/$nls"; do
        if [ -f "$candidate" ]; then
            FOUND="$candidate"
            break
        fi
    done

    if [ -n "$FOUND" ]; then
        echo "  - Copying $nls"
        cp "$FOUND" "$WORK_DIR/esp/reactos/system32/"
    else
        echo -e "${YELLOW}  - Warning: NLS file $nls not found${NC}"
    fi
done

# Create freeldr.ini based on the LiveCD configuration and force no GUI boot
LIVECD_INI_SOURCE="./boot/bootdata/livecd.ini"
LIVECD_INI_TARGET="$WORK_DIR/esp/freeldr.ini"

echo "Embedding LiveCD freeldr.ini configuration (no GUI boot)..."
if [ -f "$LIVECD_INI_SOURCE" ]; then
    python3 - "$LIVECD_INI_SOURCE" "$LIVECD_INI_TARGET" <<'PY'
import sys

source_path, target_path = sys.argv[1], sys.argv[2]

with open(source_path, 'rb') as src_file:
    raw = src_file.read()

# Normalise newlines to LF and decode with UTF-8 fallback
text = raw.decode('utf-8', errors='ignore').replace('\r\n', '\n').replace('\r', '\n')

lines = text.split('\n')
sections = []
current_header = None
current_lines = []

def flush_section():
    if current_header is None and not current_lines:
        return
    sections.append((current_header, current_lines.copy()))

for line in lines:
    trimmed = line.strip()
    if trimmed.startswith('[') and trimmed.endswith(']'):
        flush_section()
        current_header = line.strip()
        current_lines = []
    else:
        current_lines.append(line)
flush_section()

target_sections = {
    'LiveCD',
    'LiveCD_Debug',
    'LiveCD_Screen',
    'LiveCD_LogFile',
}

processed = []
for header, sec_lines in sections:
    if header is None:
        processed.append((header, sec_lines))
        continue

    name = header.strip()[1:-1]
    new_lines = []
    has_kernel = False
    has_hal = False

    for line in sec_lines:
        stripped = line.strip()
        lower = stripped.lower()

        if lower.startswith('options=') and '/noguiboot' not in lower:
            line = stripped + ' /NOGUIBOOT'

        if lower.startswith('kernel='):
            has_kernel = True
        if lower.startswith('hal='):
            has_hal = True

        new_lines.append(line)

    if name in target_sections:
        if not has_kernel:
            new_lines.append('Kernel=ntoskrnl.exe')
        if not has_hal:
            new_lines.append('Hal=hal.dll')

    processed.append((header, new_lines))

output_lines = []
for header, sec_lines in processed:
    if header:
        output_lines.append(header)
    output_lines.extend(sec_lines)

with open(target_path, 'w', encoding='utf-8') as dst_file:
    dst_file.write('\n'.join(output_lines).rstrip('\n') + '\n')
PY
else
    echo "  - Warning: LiveCD configuration not found, falling back to minimal setup"
    cat > "$LIVECD_INI_TARGET" << 'EOF'
; Fallback FreeLdr configuration for ARM64 ReactOS LiveCD

[FreeLoader]
DefaultOS=LiveCD_Debug
TimeOut=3

[Operating Systems]
LiveCD_Debug="ReactOS LiveCD (Debug)"

[LiveCD_Debug]
BootType=ReactOSSetup
SystemPath=multi(0)disk(0)rdisk(0)partition(1)\reactos
Options=/MININT /NOGUIBOOT /DEBUG /DEBUGPORT=SERIAL /BAUDRATE=115200 /SOS
Kernel=ntoskrnl.exe
Hal=hal.dll
EOF
fi

# Also copy freeldr.ini to the freeldr directory (some versions look there)
cp "$LIVECD_INI_TARGET" "$WORK_DIR/esp/freeldr/freeldr.ini"

# Create a simple startup.nsh for EFI Shell auto-execution
cat > "$WORK_DIR/esp/startup.nsh" << 'EOF'
@echo -off
echo "FreeLoader ARM64 UEFI Bootloader Environment"
echo "==========================================="
echo ""
echo "Available commands:"
echo "  fs0: - Access ESP filesystem"
echo "  ls   - List files"
echo "  cd   - Change directory"
echo "  exit - Exit shell"
echo ""
echo "To boot ReactOS, run: fs0:\EFI\BOOT\BOOTAA64.EFI"
echo ""
EOF

# Create a README for the image
cat > "$WORK_DIR/esp/README.txt" << 'EOF'
FreeLoader ARM64 UEFI LiveCD Image
===================================

This ESP image contains:
- BOOTAA64.EFI: FreeLoader ARM64 UEFI bootloader
- freeldr.ini: FreeLoader LiveCD configuration file
- startup.nsh: EFI Shell startup script

ReactOS System Files:
- /reactos/system32/ntoskrnl.exe - ReactOS ARM64 kernel
- /reactos/system32/hal.dll - Hardware Abstraction Layer
- /reactos/system32/bootvid.dll - Boot video driver
- /reactos/system32/kdcom.dll - Kernel debugger communication
- /reactos/system32/drivers/ - Essential system drivers
- /reactos/system32/config/ - Registry hives

Boot Options (LiveCD Mode):
- ReactOS LiveCD: Standard LiveCD boot (no GUI)
- ReactOS LiveCD (Debug): Serial debug output
- ReactOS LiveCD (Screen Debug): Screen debug output
- ReactOS LiveCD (Log file): Debug to log file

Important Boot Flags:
- /MININT: Minimal installation (LiveCD mode)
- /NOGUIBOOT: Disable graphical boot screen
- /KDSERIAL: Enable kernel debugger on serial port
- /FIRSTCHANCE: Break on first chance exceptions
- /SOS: Show driver names during boot

Note: Using BootType=ReactOSSetup as it's supported on ARM64.
The Windows2003 boot type is only available on x86/AMD64.

For more information, visit:
https://github.com/reactos/reactos
EOF

echo "Creating ${ESP_SIZE}MB FAT32 ESP image..."
mkdir -p "$WORK_DIR"
dd if=/dev/zero of="$ESP_IMG" bs=1M count=$ESP_SIZE status=none
mkfs.vfat -F 32 -n "FREELDR" "$ESP_IMG" >/dev/null

if command -v mcopy &> /dev/null; then
    echo "Using mtools to copy files..."
    mmd -i "$ESP_IMG" ::/EFI 2>/dev/null || true
    mmd -i "$ESP_IMG" ::/EFI/BOOT 2>/dev/null || true
    mmd -i "$ESP_IMG" ::/reactos 2>/dev/null || true
    mmd -i "$ESP_IMG" ::/reactos/system32 2>/dev/null || true
    mmd -i "$ESP_IMG" ::/reactos/system32/drivers 2>/dev/null || true
    mmd -i "$ESP_IMG" ::/reactos/system32/config 2>/dev/null || true
    mmd -i "$ESP_IMG" ::/freeldr 2>/dev/null || true

    mcopy -i "$ESP_IMG" "$WORK_DIR/esp/EFI/BOOT/BOOTAA64.EFI" ::/EFI/BOOT/
    mcopy -i "$ESP_IMG" "$WORK_DIR/esp/freeldr.ini" ::/
    mcopy -i "$ESP_IMG" "$WORK_DIR/esp/freeldr/freeldr.ini" ::/freeldr/
    mcopy -i "$ESP_IMG" "$WORK_DIR/esp/startup.nsh" ::/
    mcopy -i "$ESP_IMG" "$WORK_DIR/esp/README.txt" ::/

    # Copy ReactOS files
    if [ -f "$WORK_DIR/esp/reactos/system32/ntoskrnl.exe" ]; then
        mcopy -i "$ESP_IMG" "$WORK_DIR/esp/reactos/system32/ntoskrnl.exe" ::/reactos/system32/
    fi
    if [ -f "$WORK_DIR/esp/reactos/system32/hal.dll" ]; then
        mcopy -i "$ESP_IMG" "$WORK_DIR/esp/reactos/system32/hal.dll" ::/reactos/system32/
    fi
    if [ -f "$WORK_DIR/esp/reactos/system32/bootvid.dll" ]; then
        mcopy -i "$ESP_IMG" "$WORK_DIR/esp/reactos/system32/bootvid.dll" ::/reactos/system32/
    fi
    if [ -f "$WORK_DIR/esp/reactos/system32/kdcom.dll" ]; then
        mcopy -i "$ESP_IMG" "$WORK_DIR/esp/reactos/system32/kdcom.dll" ::/reactos/system32/
    fi

    # Copy essential NLS files
    for nls in "${NLS_FILES[@]}"; do
        if [ -f "$WORK_DIR/esp/reactos/system32/$nls" ]; then
            mcopy -i "$ESP_IMG" "$WORK_DIR/esp/reactos/system32/$nls" ::/reactos/system32/
        fi
    done

    # Copy any drivers that were found
    if [ -d "$WORK_DIR/esp/reactos/system32/drivers" ] && [ "$(ls -A $WORK_DIR/esp/reactos/system32/drivers 2>/dev/null)" ]; then
        for driver in "$WORK_DIR/esp/reactos/system32/drivers"/*.sys; do
            if [ -f "$driver" ]; then
                mcopy -i "$ESP_IMG" "$driver" ::/reactos/system32/drivers/
            fi
        done
    fi

    # Copy registry hives if they exist
    if [ -d "$WORK_DIR/esp/reactos/system32/config" ] && [ "$(ls -A $WORK_DIR/esp/reactos/system32/config 2>/dev/null)" ]; then
        for hive in "$WORK_DIR/esp/reactos/system32/config"/*; do
            if [ -f "$hive" ]; then
                mcopy -i "$ESP_IMG" "$hive" ::/reactos/system32/config/
            fi
        done
    fi

    echo -e "\n${GREEN}FreeLoader ESP image created successfully!${NC}"
    echo -e "${BLUE}Image Details:${NC}"
    echo "- Size: ${ESP_SIZE}MB"
    echo "- Filesystem: FAT32"
    echo "- Label: FREELDR"
    echo ""
    echo -e "${YELLOW}Contents:${NC}"
    mdir -i "$ESP_IMG" ::/ 2>/dev/null || true
    echo ""
    echo -e "${YELLOW}EFI/BOOT directory:${NC}"
    mdir -i "$ESP_IMG" ::/EFI/BOOT/ 2>/dev/null || true
    echo ""
    echo -e "${YELLOW}ReactOS system files:${NC}"
    mdir -i "$ESP_IMG" ::/reactos/system32/ 2>/dev/null || true
    if [ -d "$WORK_DIR/esp/reactos/system32/drivers" ] && [ "$(ls -A $WORK_DIR/esp/reactos/system32/drivers 2>/dev/null)" ]; then
        echo ""
        echo -e "${YELLOW}ReactOS drivers:${NC}"
        mdir -i "$ESP_IMG" ::/reactos/system32/drivers/ 2>/dev/null || true
    fi
    if [ -d "$WORK_DIR/esp/reactos/system32/config" ] && [ "$(ls -A $WORK_DIR/esp/reactos/system32/config 2>/dev/null)" ]; then
        echo ""
        echo -e "${YELLOW}ReactOS registry hives:${NC}"
        mdir -i "$ESP_IMG" ::/reactos/system32/config/ 2>/dev/null || true
    fi
else
    echo "mtools not found. Using mount (requires sudo)..."
    sudo mkdir -p /mnt/freeldr_esp
    sudo mount -o loop "$ESP_IMG" /mnt/freeldr_esp
    sudo cp -r "$WORK_DIR/esp"/* /mnt/freeldr_esp/
    sudo umount /mnt/freeldr_esp
    echo -e "\n${GREEN}FreeLoader ESP image created successfully!${NC}"
fi

echo -e "\n${GREEN}Image location: $ESP_IMG${NC}"
echo -e "\n${BLUE}To test with QEMU, run:${NC}"
echo "qemu-system-aarch64 \\"
echo "    -M virt \\"
echo "    -cpu cortex-a72 \\"
echo "    -m 4G \\"
echo "    -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \\"
echo "    -drive file=$ESP_IMG,format=raw,if=none,id=boot \\"
echo "    -device virtio-blk-pci,drive=boot,bootindex=0 \\"
echo "    -device qemu-xhci \\"
echo "    -device usb-kbd \\"
echo "    -device usb-tablet \\"
echo "    -device ramfb \\"
echo "    -display $QEMU_DISPLAY \\"
echo "    -serial file:$QEMU_SERIAL"

echo -e "\n${YELLOW}Starting QEMU test...${NC}"

# Run QEMU with internal timeout so callers don't need an outer timeout
if command -v timeout >/dev/null 2>&1; then
  TIMEOUT_CMD=(timeout "${QEMU_TIMEOUT}")
else
  TIMEOUT_CMD=()
fi

echo -e "${GREEN}Running FreeLoader in QEMU for ${QEMU_TIMEOUT} seconds...${NC}"
echo "Serial output will be logged to: $QEMU_SERIAL"

"${TIMEOUT_CMD[@]}" qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a72 \
  -m 4G \
  -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
  -drive file="$ESP_IMG",format=raw,if=none,id=boot \
  -device virtio-blk-pci,drive=boot,bootindex=0 \
  -device qemu-xhci \
  -device usb-kbd \
  -device usb-tablet \
  -device ramfb \
  -display "$QEMU_DISPLAY" \
  -serial file:"$QEMU_SERIAL" \
  ${QEMU_EXTRA} || true

# Show the first 300 lines of the serial log if available
if [ -f "$QEMU_SERIAL" ]; then
  echo -e "\n${YELLOW}--- Serial Output (first 300 lines) ---${NC}"
  sed -n '1,300p' "$QEMU_SERIAL"
  echo -e "\n${GREEN}Full serial log available at: $QEMU_SERIAL${NC}"
else
  echo -e "\n${YELLOW}No serial output captured.${NC}"
fi

echo -e "\n${GREEN}FreeLoader test completed!${NC}"
echo -e "${BLUE}Image ready for deployment: $ESP_IMG${NC}"
