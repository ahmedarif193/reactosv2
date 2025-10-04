/* Host-side PNG to BGRA converter using libspng */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "spng.h"

static void rgba_to_bgra(uint8_t* p, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        uint8_t r = p[i*4+0];
        uint8_t g = p[i*4+1];
        uint8_t b = p[i*4+2];
        uint8_t a = p[i*4+3];
        p[i*4+0] = b;
        p[i*4+1] = g;
        p[i*4+2] = r;
        p[i*4+3] = a; /* keep alpha for completeness, consumer may ignore */
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <in.png> <out.bgra>\n", argv[0]);
        return 2;
    }
    const char* in_path = argv[1];
    const char* out_path = argv[2];

    FILE* f = fopen(in_path, "rb");
    if (!f) { perror("fopen"); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); fprintf(stderr, "empty file\n"); return 1; }
    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if (!buf) { fclose(f); fprintf(stderr, "oom\n"); return 1; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(buf); fprintf(stderr, "read fail\n"); return 1; }
    fclose(f);

    spng_ctx* ctx = spng_ctx_new(0);
    if (!ctx) { free(buf); fprintf(stderr, "spng_ctx_new failed\n"); return 1; }
    spng_set_crc_action(ctx, SPNG_CRC_USE, SPNG_CRC_USE);
    if (spng_set_png_buffer(ctx, buf, (size_t)sz) != 0)
    { spng_ctx_free(ctx); free(buf); fprintf(stderr, "set_png_buffer failed\n"); return 1; }

    struct spng_ihdr ihdr;
    if (spng_get_ihdr(ctx, &ihdr) != 0)
    { spng_ctx_free(ctx); free(buf); fprintf(stderr, "get_ihdr failed\n"); return 1; }

    size_t out_size = 0;
    if (spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &out_size) != 0)
    { spng_ctx_free(ctx); free(buf); fprintf(stderr, "size failed\n"); return 1; }

    uint8_t* rgba = (uint8_t*)malloc(out_size);
    if (!rgba) { spng_ctx_free(ctx); free(buf); fprintf(stderr, "oom rgba\n"); return 1; }
    if (spng_decode_image(ctx, rgba, out_size, SPNG_FMT_RGBA8, 0) != 0)
    { spng_ctx_free(ctx); free(buf); free(rgba); fprintf(stderr, "decode failed\n"); return 1; }
    spng_ctx_free(ctx);
    free(buf);

    rgba_to_bgra(rgba, (size_t)ihdr.width * (size_t)ihdr.height);

    FILE* fo = fopen(out_path, "wb");
    if (!fo) { perror("fopen out"); free(rgba); return 1; }
    size_t wrote = fwrite(rgba, 1, out_size, fo);
    fclose(fo);
    free(rgba);
    if (wrote != out_size) { fprintf(stderr, "write failed\n"); return 1; }

    /* Print metadata to stdout: width height pitch */
    printf("%u %u %u\n", (unsigned)ihdr.width, (unsigned)ihdr.height, (unsigned)(ihdr.width * 4));
    return 0;
}

