#!/usr/bin/env python3
"""
Purge Windows RTL function bloat from Uefinity
Replace with clean, standard C functions
"""

import os
import re
import glob

def purge_rtl_functions(file_path):
    """Replace RTL functions with standard C equivalents"""
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()

        original_content = content

        # Replace RTL functions with standard C functions
        replacements = [
            # RtlZeroMemory(ptr, size) -> memset(ptr, 0, size)
            (r'RtlZeroMemory\s*\(\s*([^,]+)\s*,\s*([^)]+)\s*\)', r'memset(\1, 0, \2)'),

            # RtlFillMemory(ptr, size, val) -> memset(ptr, val, size)
            (r'RtlFillMemory\s*\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^)]+)\s*\)', r'memset(\1, \3, \2)'),

            # RtlCopyMemory(dst, src, size) -> memcpy(dst, src, size)
            (r'RtlCopyMemory\s*\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^)]+)\s*\)', r'memcpy(\1, \2, \3)'),

            # RtlMoveMemory(dst, src, size) -> memmove(dst, src, size)
            (r'RtlMoveMemory\s*\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^)]+)\s*\)', r'memmove(\1, \2, \3)'),
        ]

        changes_made = 0
        for pattern, replacement in replacements:
            new_content, count = re.subn(pattern, replacement, content)
            if count > 0:
                content = new_content
                changes_made += count

        # Write back if changes were made
        if content != original_content:
            with open(file_path, 'w', encoding='utf-8') as f:
                f.write(content)
            print(f"✅ {file_path}: {changes_made} RTL functions purged")
            return changes_made
        else:
            return 0

    except Exception as e:
        print(f"❌ Error processing {file_path}: {e}")
        return 0

def main():
    """Purge RTL functions from all Uefinity source files"""
    base_dir = "/home/ahmed/WorkDir/reactos_arm64/boot/uefinity"

    # Find all C/C++ source files
    patterns = [
        f"{base_dir}/**/*.c",
        f"{base_dir}/**/*.cpp",
        f"{base_dir}/**/*.h",
    ]

    total_files = 0
    total_changes = 0

    print("🔥 Purging Windows RTL function bloat from Uefinity...")
    print("=" * 60)

    for pattern in patterns:
        for file_path in glob.glob(pattern, recursive=True):
            # Skip 3rdparty directory
            if "/3rdparty/" in file_path:
                continue

            changes = purge_rtl_functions(file_path)
            if changes > 0:
                total_files += 1
                total_changes += changes

    print("=" * 60)
    print(f"🎉 RTL Purge Complete!")
    print(f"📁 Files modified: {total_files}")
    print(f"🔄 Total replacements: {total_changes}")
    print("💪 Uefinity is now free of Windows convention bloat!")

if __name__ == "__main__":
    main()