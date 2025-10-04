#!/usr/bin/env python3
"""
Fix RTL Function Syntax Issues
Corrects syntax errors that may have occurred during conversion
"""

import os
import re
import glob

def fix_rtl_syntax(file_path):
    """Fix common syntax issues in RTL function conversions"""
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            content = f.read()

        original_content = content

        # Fix RtlFillMemory syntax issues - incorrect parameter order
        # Look for RtlFillMemory(ptr, size, value) where value has commas
        fixes = [
            # Fix cases where the fill value got corrupted during conversion
            (r'RtlFillMemory\s*\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^,]*)\s*,\s*([^)]+)\s*\)',
             r'RtlFillMemory(\1, \2, \4)'),

            # Fix 0xCCCCCCCC pattern specifically
            (r'RtlFillMemory\s*\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*0xCCCCCCCC\s*\)',
             r'RtlFillMemory(\1, \2, 0xCC)'),
        ]

        changes_made = 0
        for pattern, replacement in fixes:
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
        print(f"❌ Error fixing {file_path}: {e}")
        return 0

def main():
    """Fix RTL syntax issues throughout Uefinity"""
    base_dir = "/home/ahmed/WorkDir/reactos_arm64/boot/uefinity"

    patterns = [
        f"{base_dir}/**/*.c",
        f"{base_dir}/**/*.cpp",
    ]

    total_files = 0
    total_changes = 0

    print("🔧 Fixing RTL function syntax issues...")
    print("=" * 50)

    for pattern in patterns:
        for file_path in glob.glob(pattern, recursive=True):
            # Skip 3rdparty directory
            if "/3rdparty/" in file_path:
                continue

            changes = fix_rtl_syntax(file_path)
            if changes > 0:
                total_files += 1
                total_changes += changes
                print(f"✅ {file_path}: {changes} syntax fixes")

    print("=" * 50)
    print(f"🎉 RTL Syntax Fix Complete!")
    print(f"📁 Files fixed: {total_files}")
    print(f"🔄 Total fixes: {total_changes}")

if __name__ == "__main__":
    main()