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
