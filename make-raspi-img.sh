#!/bin/bash

# Create Raspberry Pi UEFI bootable image with ReactOS contents
# Usage: ./make-raspi-img.sh
# Optional environment overrides:
#   BUILD_DIR          - ReactOS build directory (default: ./output-MinGW-arm64)
#   RPI_FIRMWARE_DIR   - Directory containing Raspberry Pi UEFI firmware files
#                        (auto-detected; defaults to output-MinGW-arm64/raspberry_firmware,
#                        ./RPi3_UEFI_Firmware_v1.39, or ../reactos_backup_orig/RPi3_UEFI_Firmware_v1.39)
#   RASPI_IMG_SIZE     - Size of the FAT32 image in MB (default: 512)

set -euo pipefail

# Colours
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

BUILD_DIR="${BUILD_DIR:-./output-MinGW-arm64}"

if [ -n "${RPI_FIRMWARE_DIR:-}" ]; then
    FIRMWARE_DIR="$RPI_FIRMWARE_DIR"
else
    _firmware_candidates=(
        "${BUILD_DIR}/raspberry_firmware"
        "./RPi3_UEFI_Firmware_v1.39"
        "../reactos_backup_orig/RPi3_UEFI_Firmware_v1.39"
    )
    FIRMWARE_DIR=""

    for candidate in "${_firmware_candidates[@]}"; do
        if [ -d "$candidate" ] && [ -f "$candidate/RPI_EFI.fd" ]; then
            FIRMWARE_DIR="$candidate"
            break
        fi
    done

    if [ -z "$FIRMWARE_DIR" ]; then
        for candidate in "${_firmware_candidates[@]}"; do
            if [ -d "$candidate" ]; then
                FIRMWARE_DIR="$candidate"
                break
            fi
        done
    fi

    if [ -z "$FIRMWARE_DIR" ]; then
        FIRMWARE_DIR="${_firmware_candidates[0]}"
    fi
    unset candidate _firmware_candidates
fi
WORK_DIR="${BUILD_DIR}/raspi_image"
STAGING_DIR="${WORK_DIR}/staging"
UEFI_LOADER="${BUILD_DIR}/boot/freeldr/freeldr/uefildr.efi"
IMG_PATH="${WORK_DIR}/raspberry-pi-uefi.img"
IMG_SIZE="${RASPI_IMG_SIZE:-512}"

ensure_build_artifacts() {
    echo -e "${GREEN}Building FreeLoader ARM64 UEFI bootloader...${NC}"
    pushd "$BUILD_DIR" >/dev/null
    if ninja uefildr; then
        echo -e "${GREEN}uefildr.efi build successful!${NC}"
    else
        echo -e "${RED}uefildr.efi build failed${NC}"
        exit 1
    fi
    popd >/dev/null

    if [ ! -f "$UEFI_LOADER" ]; then
        echo -e "${RED}FreeLoader UEFI loader not found at $UEFI_LOADER${NC}"
        exit 1
    fi

    if [ ! -d "$FIRMWARE_DIR" ]; then
        echo -e "${RED}Raspberry Pi firmware directory not found: $FIRMWARE_DIR${NC}"
        echo "Hint: run ./download-raspi-firmware.sh first or place firmware under one of these paths:"
        echo "  - ${BUILD_DIR}/raspberry_firmware"
        echo "  - ./RPi3_UEFI_Firmware_v1.39"
        echo "  - ../reactos_backup_orig/RPi3_UEFI_Firmware_v1.39"
        exit 1
    fi

    if [ ! -f "$FIRMWARE_DIR/RPI_EFI.fd" ]; then
        echo -e "${RED}RPI_EFI.fd not found under $FIRMWARE_DIR${NC}"
        exit 1
    fi
}

prepare_staging_dir() {
    echo -e "${GREEN}Staging Raspberry Pi boot files...${NC}"
    rm -rf "$STAGING_DIR"
    mkdir -p "$STAGING_DIR"

    echo "Copying Raspberry Pi UEFI firmware set from $FIRMWARE_DIR"
    cp -a "$FIRMWARE_DIR"/. "$STAGING_DIR"/

    mkdir -p "$STAGING_DIR/EFI/BOOT"
    mkdir -p "$STAGING_DIR/reactos/system32"
    mkdir -p "$STAGING_DIR/reactos/system32/drivers"
    mkdir -p "$STAGING_DIR/reactos/system32/config"
    mkdir -p "$STAGING_DIR/freeldr"
}

copy_reactos_components() {
    echo "Copying ReactOS kernel and HAL..."
    if [ -f "$BUILD_DIR/ntoskrnl/ntoskrnl.exe" ]; then
        cp "$BUILD_DIR/ntoskrnl/ntoskrnl.exe" "$STAGING_DIR/reactos/system32/"
    else
        echo -e "${YELLOW}  - Warning: ntoskrnl.exe not found${NC}"
    fi

    if [ -f "$BUILD_DIR/hal/halarm64/hal.dll" ]; then
        cp "$BUILD_DIR/hal/halarm64/hal.dll" "$STAGING_DIR/reactos/system32/"
    else
        echo -e "${YELLOW}  - Warning: hal.dll not found${NC}"
    fi

    if [ -d "$BUILD_DIR/boot/bootdata" ]; then
        echo "Copying bootdata files..."
        for hive in system software default sam security; do
            if [ -f "$BUILD_DIR/boot/bootdata/$hive" ]; then
                cp "$BUILD_DIR/boot/bootdata/$hive" "$STAGING_DIR/reactos/system32/config/"
            fi
        done

        if [ -f "$BUILD_DIR/boot/bootdata/hivesys.inf" ]; then
            cp "$BUILD_DIR/boot/bootdata/hivesys.inf" "$STAGING_DIR/reactos/system32/"
        fi
    fi

    echo "Copying kernel-mode drivers..."
    local drivers_path="$BUILD_DIR/drivers"
    if [ -d "$drivers_path" ]; then
        while IFS= read -r -d '' driver; do
            cp "$driver" "$STAGING_DIR/reactos/system32/drivers/"
        done < <(find "$drivers_path" -type f -name '*.sys' -print0)
    else
        echo -e "${YELLOW}  - Warning: drivers directory not found${NC}"
    fi

    local critical_driver_list
    critical_driver_list=$(python3 - <<'PY'
import re
from pathlib import Path

inf_path = Path('boot/bootdata/hivesys.inf')
critical = set()
pattern = re.compile(r'system32\\drivers\\([^"\\]+\.sys)', re.IGNORECASE)

if inf_path.exists():
    with inf_path.open('r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            match = pattern.search(line)
            if match:
                critical.add(match.group(1).lower())

print('\n'.join(sorted(critical)))
PY
)

    if [ -n "$critical_driver_list" ]; then
        echo "Ensuring boot-critical drivers are staged..."
        while IFS= read -r driver_name; do
            [ -z "$driver_name" ] && continue
            local dest="$STAGING_DIR/reactos/system32/drivers/${driver_name}"
            if [ -f "$dest" ]; then
                continue
            fi

            local src
            src=$(find "$BUILD_DIR" -type f -name "$driver_name" -print -quit)
            if [ -n "$src" ]; then
                cp "$src" "$dest"
            else
                echo -e "${YELLOW}  - Warning: boot-critical driver ${driver_name} not found${NC}"
            fi
        done <<< "$critical_driver_list"
    fi

    if [ -f "$BUILD_DIR/drivers/base/bootvid/bootvid.dll" ]; then
        cp "$BUILD_DIR/drivers/base/bootvid/bootvid.dll" "$STAGING_DIR/reactos/system32/"
    fi

    if [ -f "$BUILD_DIR/drivers/base/kdcom/kdcom.dll" ]; then
        cp "$BUILD_DIR/drivers/base/kdcom/kdcom.dll" "$STAGING_DIR/reactos/system32/"
    fi

    echo "Copying NLS data files..."
    local nls_files=(
        "c_1252.nls"
        "c_437.nls"
        "l_intl.nls"
        "locale.nls"
        "ctype.nls"
        "sortkey.nls"
        "sorttbls.nls"
    )

    for nls in "${nls_files[@]}"; do
        local found=""
        for candidate in \
            "$BUILD_DIR/reactos/system32/$nls" \
            "$BUILD_DIR/media/nls/$nls" \
            "./media/nls/$nls"; do
            if [ -f "$candidate" ]; then
                found="$candidate"
                break
            fi
        done

        if [ -n "$found" ]; then
            cp "$found" "$STAGING_DIR/reactos/system32/"
        else
            echo -e "${YELLOW}  - Warning: NLS file $nls not found${NC}"
        fi
    done
}

create_freeldr_ini() {
    local source="./boot/bootdata/livecd.ini"
    local target="$STAGING_DIR/freeldr.ini"

    echo "Preparing freeldr.ini configuration..."

    if [ -f "$source" ]; then
        python3 - "$source" "$target" <<'PY'
import sys

source_path, target_path = sys.argv[1], sys.argv[2]

with open(source_path, 'rb') as src_file:
    raw = src_file.read()

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

        if 'ARM64TESTCRASH' in stripped:
            stripped = stripped.replace(' /ARM64TESTCRASH', '').replace('/ARM64TESTCRASH', '')

        if lower.startswith('options=') and '/noguiboot' not in lower:
            line = stripped + ' /NOGUIBOOT'
        else:
            line = stripped

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
        cat > "$target" <<'EOF'
; Fallback FreeLdr configuration for ARM64 ReactOS on Raspberry Pi

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

    cp "$target" "$STAGING_DIR/freeldr/freeldr.ini"
}

create_startup_files() {
    cat > "$STAGING_DIR/startup.nsh" <<'EOF'
@echo -off
echo "ReactOS ARM64 Raspberry Pi Boot Environment"
echo "==========================================="
echo ""
echo "To boot ReactOS, run: fs0:\EFI\BOOT\BOOTAA64.EFI"
echo ""
EOF

    cat > "$STAGING_DIR/README.txt" <<'EOF'
ReactOS ARM64 Raspberry Pi Image
=================================

This image contains Raspberry Pi UEFI firmware and ReactOS ARM64 system files.

Key files:
- RPI_EFI.fd: Raspberry Pi UEFI firmware
- EFI/BOOT/BOOTAA64.EFI: FreeLoader ARM64 UEFI bootloader
- freeldr.ini: Boot configuration (duplicated under /freeldr)
- reactos/system32: ReactOS core binaries, drivers, and NLS data
- reactos/system32/config: ReactOS registry hives

Flash this image to an SD card and place it in the Raspberry Pi.
For serial debugging, enable UART in config.txt as needed.
EOF
}

stage_bootloader() {
    cp "$UEFI_LOADER" "$STAGING_DIR/EFI/BOOT/BOOTAA64.EFI"
}

create_image() {
    echo -e "${GREEN}Creating ${IMG_SIZE}MB Raspberry Pi FAT32 image...${NC}"
    mkdir -p "$WORK_DIR"
    rm -f "$IMG_PATH"
    dd if=/dev/zero of="$IMG_PATH" bs=1M count="$IMG_SIZE" status=none
    mkfs.vfat -F 32 -n "ROSPI" "$IMG_PATH" >/dev/null

    if command -v mcopy &>/dev/null; then
        echo "Using mtools to populate image..."
        shopt -s nullglob
        local entries=("$STAGING_DIR"/*)
        shopt -u nullglob
        if [ ${#entries[@]} -eq 0 ]; then
            echo -e "${RED}Nothing to copy into the image${NC}"
            exit 1
        fi
        for entry in "${entries[@]}"; do
            mcopy -s -i "$IMG_PATH" "$entry" ::/
        done
    else
        echo "mtools not found. Using mount (requires sudo)..."
        sudo mkdir -p /mnt/reactos_raspi
        sudo mount -o loop "$IMG_PATH" /mnt/reactos_raspi
        sudo cp -a "$STAGING_DIR"/. /mnt/reactos_raspi/
        sudo umount /mnt/reactos_raspi
        sudo rmdir /mnt/reactos_raspi
    fi

    echo -e "${GREEN}Raspberry Pi image created at $IMG_PATH${NC}"

    if command -v mdir &>/dev/null; then
        echo -e "${BLUE}Listing top-level image contents:${NC}"
        mdir -i "$IMG_PATH" ::/
    fi
}

main() {
    ensure_build_artifacts
    prepare_staging_dir
    stage_bootloader
    copy_reactos_components
    create_freeldr_ini
    create_startup_files
    create_image
}

main
