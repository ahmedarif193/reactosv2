#!/usr/bin/env python3
import sys
import os
import argparse
import glob
import subprocess

def safe_name(path):
    base = os.path.basename(path)
    name = ''.join(c if c.isalnum() or c in ('_',) else '_' for c in base)
    if name[0].isdigit():
        name = '_' + name
    return name

def emit_assets(sources, out_c, out_h, png2bgra=None):
    os.makedirs(os.path.dirname(out_c), exist_ok=True)
    os.makedirs(os.path.dirname(out_h), exist_ok=True)

    with open(out_h, 'w') as hh:
        hh.write("""
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef enum EMBEDDED_ASSET_KIND {
    ASSET_RAW_BGRA8 = 1,
    ASSET_PNG = 2
} EMBEDDED_ASSET_KIND;

typedef struct EMBEDDED_ASSET {
    const char* name;          /* filename (basename) */
    const uint8_t* data;       /* raw bytes (BGRA8 or PNG) */
    size_t size;               /* length in bytes */
    uint32_t width;            /* for RAW_BGRA8 */
    uint32_t height;           /* for RAW_BGRA8 */
    uint32_t pitch;            /* bytes per row for RAW_BGRA8 */
    uint32_t kind;             /* EMBEDDED_ASSET_KIND */
} EMBEDDED_ASSET;

extern const EMBEDDED_ASSET gEmbeddedAssets[];
extern const size_t gEmbeddedAssetCount;

const EMBEDDED_ASSET* FindEmbeddedAsset(const char* name);
""".lstrip())

    with open(out_c, 'w') as cc:
        cc.write("""
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "ui/assets.h"

""".lstrip())

        entries = []
        for idx, path in enumerate(sources):
            blob = None
            base = os.path.basename(path)
            sym = f"g_asset_{safe_name(path)}"
            width = height = pitch = 0
            kind = 2  # default PNG
            # Try host-side predecode if tool available
            if png2bgra and os.path.exists(png2bgra):
                tmpout = f"{out_c}.tmp.{idx}.bgra"
                try:
                    cp = subprocess.run([png2bgra, path, tmpout], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True, text=True)
                    parts = cp.stdout.strip().split()
                    if len(parts) >= 3 and os.path.exists(tmpout):
                        width = int(parts[0]); height = int(parts[1]); pitch = int(parts[2])
                        with open(tmpout, 'rb') as tf:
                            blob = tf.read()
                        kind = 1  # RAW BGRA
                except Exception:
                    blob = None
                finally:
                    try:
                        if os.path.exists(tmpout): os.remove(tmpout)
                    except Exception:
                        pass
            if blob is None:
                with open(path, 'rb') as f:
                    blob = f.read()
                kind = 2
            cc.write(f"static const uint8_t {sym}[] = {{\n")
            for i in range(0, len(blob), 16):
                chunk = blob[i:i+16]
                line = ', '.join(f"0x{b:02x}" for b in chunk)
                cc.write(f"    {line},\n")
            cc.write("};\n\n")
            entries.append((base, sym, len(blob), width, height, pitch, kind))

        cc.write("const EMBEDDED_ASSET gEmbeddedAssets[] = {\n")
        for name, sym, size, w, h, p, kind in entries:
            cc.write(f"    {{ \"{name}\", {sym}, {size}, {w}, {h}, {p}, {kind} }},\n")
        cc.write("};\n")
        cc.write(f"const size_t gEmbeddedAssetCount = {len(entries)};\n\n")

        cc.write("""
const EMBEDDED_ASSET* FindEmbeddedAsset(const char* name)
{
    if (!name) return NULL;
    for (size_t i = 0; i < gEmbeddedAssetCount; ++i)
    {
        if (strcmp(gEmbeddedAssets[i].name, name) == 0)
            return &gEmbeddedAssets[i];
    }
    return NULL;
}
""")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--asset-dir', required=True)
    ap.add_argument('--out-c', required=True)
    ap.add_argument('--out-h', required=True)
    ap.add_argument('--png2bgra', required=False)
    args = ap.parse_args()

    patterns = ['*.png', '*.PNG', '*.Png', '*.pNg', '*.pnG']
    pngs = []
    for pat in patterns:
        pngs.extend(glob.glob(os.path.join(args.asset_dir, pat)))
    pngs = sorted(set(pngs))
    emit_assets(pngs, args.out_c, args.out_h, png2bgra=args.png2bgra)

if __name__ == '__main__':
    main()
