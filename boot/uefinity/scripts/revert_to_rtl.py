#!/usr/bin/env python3
"""
Revert Standard C Functions to RTL Names in Uefinity
Intelligently converts standard C memory functions back to RTL equivalents
while leveraging the new RTL compatibility layer for optimal performance.

This script reverses the work of purge_rtl.py but with the benefit of
the optimized RTL compatibility layer.
"""

import os
import re
import glob
import sys

def add_rtl_include(file_path):
    """Add RTL compatibility header include if not present"""
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()

        # Check if already has the include
        if '#include "rtl_compat.h"' in content or '#include <rtl_compat.h>' in content:
            return False

        # Find the best place to insert the include
        lines = content.split('\n')
        insert_pos = 0

        # Look for existing includes
        for i, line in enumerate(lines):
            if line.strip().startswith('#include'):
                insert_pos = i + 1
            elif line.strip().startswith('#pragma once') or line.strip().startswith('#ifndef'):
                if insert_pos == 0:
                    insert_pos = i + 1

        # If no includes found, insert after initial comments/pragmas
        if insert_pos == 0:
            for i, line in enumerate(lines):
                if not (line.strip().startswith('/*') or
                       line.strip().startswith('*') or
                       line.strip().startswith('//') or
                       line.strip().startswith('#pragma') or
                       line.strip() == ''):
                    insert_pos = i
                    break

        # Insert the include
        if insert_pos < len(lines):
            lines.insert(insert_pos, '#include "rtl_compat.h"')

            # Write back the modified content
            with open(file_path, 'w', encoding='utf-8') as f:
                f.write('\n'.join(lines))
            return True

    except Exception as e:
        print(f"❌ Error adding include to {file_path}: {e}")
        return False

    return False

def revert_to_rtl_functions(file_path):
    """Replace standard C functions with RTL equivalents"""
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()

        original_content = content

        # Smart replacements - only convert when it makes sense
        replacements = [
            # memset(ptr, 0, size) -> RtlZeroMemory(ptr, size)
            (r'\bmemset\s*\(\s*([^,]+)\s*,\s*0\s*,\s*([^)]+)\s*\)', r'RtlZeroMemory(\1, \2)'),

            # memset(ptr, val, size) -> RtlFillMemory(ptr, size, val) - only for non-zero values
            (r'\bmemset\s*\(\s*([^,]+)\s*,\s*([^0][^,]*)\s*,\s*([^)]+)\s*\)', r'RtlFillMemory(\1, \3, \2)'),

            # memcpy(dst, src, size) -> RtlCopyMemory(dst, src, size)
            (r'\bmemcpy\s*\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^)]+)\s*\)', r'RtlCopyMemory(\1, \2, \3)'),

            # memmove(dst, src, size) -> RtlMoveMemory(dst, src, size)
            (r'\bmemmove\s*\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^)]+)\s*\)', r'RtlMoveMemory(\1, \2, \3)'),
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
            return changes_made
        else:
            return 0

    except Exception as e:
        print(f"❌ Error processing {file_path}: {e}")
        return 0

def should_process_file(file_path):
    """Determine if a file should be processed"""
    # Skip 3rdparty directory
    if "/3rdparty/" in file_path:
        return False

    # Skip the RTL compatibility header itself
    if "rtl_compat.h" in file_path:
        return False

    # Skip scripts directory
    if "/scripts/" in file_path:
        return False

    return True

def update_freeldr_header():
    """Update the main freeldr.h to include RTL compatibility"""
    freeldr_path = "/home/ahmed/WorkDir/reactos_arm64/boot/uefinity/include/freeldr.h"

    try:
        with open(freeldr_path, 'r', encoding='utf-8') as f:
            content = f.read()

        # Check if already included
        if '#include "rtl_compat.h"' in content:
            return False

        # Find the right place to insert - after mm.h include
        lines = content.split('\n')
        for i, line in enumerate(lines):
            if '#include <mm.h>' in line or '#include "mm.h"' in line:
                lines.insert(i + 1, '#include "rtl_compat.h"')
                break
        else:
            # Fallback: insert after internal headers comment
            for i, line in enumerate(lines):
                if "/* Internal headers */" in line:
                    lines.insert(i + 1, '#include "rtl_compat.h"')
                    break

        with open(freeldr_path, 'w', encoding='utf-8') as f:
            f.write('\n'.join(lines))

        print(f"✅ Updated {freeldr_path} to include RTL compatibility layer")
        return True

    except Exception as e:
        print(f"❌ Error updating freeldr.h: {e}")
        return False

def main():
    """Revert standard C functions to RTL names throughout Uefinity"""
    base_dir = "/home/ahmed/WorkDir/reactos_arm64/boot/uefinity"

    # First, update the main header
    update_freeldr_header()

    # Find all C/C++ source files
    patterns = [
        f"{base_dir}/**/*.c",
        f"{base_dir}/**/*.cpp",
        f"{base_dir}/**/*.h",
    ]

    total_files_processed = 0
    total_files_with_includes = 0
    total_function_changes = 0

    print("🔄 Reverting to RTL functions with optimized compatibility layer...")
    print("=" * 70)

    for pattern in patterns:
        for file_path in glob.glob(pattern, recursive=True):
            if not should_process_file(file_path):
                continue

            # Add RTL compatibility include
            include_added = add_rtl_include(file_path)
            if include_added:
                total_files_with_includes += 1

            # Convert functions to RTL equivalents
            changes = revert_to_rtl_functions(file_path)
            if changes > 0:
                total_files_processed += 1
                total_function_changes += changes
                print(f"✅ {file_path}: {changes} functions converted to RTL")

    print("=" * 70)
    print(f"🎉 RTL Compatibility Restoration Complete!")
    print(f"📁 Files with function changes: {total_files_processed}")
    print(f"📄 Files with new includes: {total_files_with_includes}")
    print(f"🔄 Total function conversions: {total_function_changes}")
    print()
    print("💡 Benefits of this approach:")
    print("   • Maintains ReactOS ecosystem compatibility")
    print("   • Provides ARM64-optimized implementations")
    print("   • Preserves RTL naming for debugging tools")
    print("   • Zero performance overhead (inline functions)")
    print("   • Easy to maintain and understand")
    print("   • UEFI-aware with proper cache management")
    print()
    print("🏗️  Next steps:")
    print("   1. Build and test the codebase")
    print("   2. Verify ARM64 performance improvements")
    print("   3. Check ReactOS component compatibility")

if __name__ == "__main__":
    main()