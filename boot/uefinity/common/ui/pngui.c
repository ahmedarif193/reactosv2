/*
 * UEFI PNG UI helper using libspng
 */

#include <uefildr.h>
#include <debug.h>
#include <ui/assets.h>

#include <spng.h>
#include <stdint.h>
#include <string.h>

#include <ui/display.h>
#include <ui/draw.h>
#include <ui/menu.h>
#include <ui/page_static.h>
#include <ui/page_log.h>

DBG_DEFAULT_CHANNEL(WARNING);

extern EFI_SYSTEM_TABLE* GlobalSystemTable;


/* Public: render an embedded PNG that covers the screen, anchored to the top-left edge */
BOOLEAN UiShowPngFullScreen(const char* asset_name)
{
    UI_DisplayContext display;
    EFI_STATUS st;
    const EMBEDDED_ASSET* asset = FindEmbeddedAsset(asset_name);

    st = UiDisplayAcquire(&display);
    if (EFI_ERROR(st) || !display.gop)
    {
        ERR("UiShowPngFullScreen: GOP not available: %I64x\n", (unsigned long long)(UINTN)st);
        return FALSE;
    }

    unsigned screen_w = display.screen_width;
    unsigned screen_h = display.screen_height;

    /* If no embedded asset found, fall back to a flat fill so there is always a visual. */
    if (!asset)
    {
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL px = UiDisplayMakeColor(0x20, 0x20, 0x20);
        UiDisplayFillRect(&display, 0, 0, screen_w, screen_h, px);
        TRACE("[PNG] No asset '%s'; drew fallback background\n", asset_name ? asset_name : "(null)");
        return TRUE;
    }

    /* If asset is already predecoded BGRA, BLT directly */
    if (asset && asset->kind == ASSET_RAW_BGRA8 && asset->data && asset->width && asset->height)
    {
        unsigned dw = asset->width;
        unsigned dh = asset->height;
        unsigned tw = dw;
        unsigned th = dh;

        UiComputeCoverDimensions(dw, dh, screen_w, screen_h, &tw, &th);

        if (tw == 0 || th == 0)
            return FALSE;

        EFI_BOOT_SERVICES* bs = display.boot_services;
        VOID* blt_buf = NULL;
        size_t blt_size = (size_t)tw * (size_t)th * 4;
        size_t src_size = (size_t)dw * (size_t)dh * 4;
        if (!bs || EFI_ERROR(bs->AllocatePool(EfiLoaderData, blt_size, &blt_buf)))
            return FALSE;

        if (tw == dw && th == dh)
        {
            size_t copy_size = src_size < blt_size ? src_size : blt_size;
            memcpy(blt_buf, asset->data, copy_size);
        }
        else
        {
            /* resample BGRA8 (treat as RGBA in helper and keep channels) */
            VOID* tmp_rgba = NULL;
            if (!bs || EFI_ERROR(bs->AllocatePool(EfiLoaderData, src_size, &tmp_rgba)))
            { bs->FreePool(blt_buf); return FALSE; }
            /* convert BGRA->RGBA into tmp, reuse Resample and then back to BGRA in-place */
            const uint8_t* src = (const uint8_t*)asset->data;
            uint8_t* rgba = (uint8_t*)tmp_rgba;
            size_t pix = (size_t)dw * (size_t)dh;
            for (size_t i = 0; i < pix; ++i)
            {
                rgba[i*4+0] = src[i*4+2];
                rgba[i*4+1] = src[i*4+1];
                rgba[i*4+2] = src[i*4+0];
                rgba[i*4+3] = src[i*4+3];
            }
            VOID* scaled = NULL;
            if (!bs || EFI_ERROR(bs->AllocatePool(EfiLoaderData, blt_size, &scaled)))
            { bs->FreePool(tmp_rgba); bs->FreePool(blt_buf); return FALSE; }
            UiResampleRGBA8((const uint8_t*)tmp_rgba, dw, dh, (uint8_t*)scaled, tw, th);
            UiRGBAtoBGRA((const uint8_t*)scaled, (uint8_t*)blt_buf, tw, th);
            if (bs) bs->FreePool(tmp_rgba);
            if (bs) bs->FreePool(scaled);
        }

        UINTN delta = (UINTN)tw * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL);
        EFI_STATUS bltst = display.gop->Blt(display.gop,
                                            (EFI_GRAPHICS_OUTPUT_BLT_PIXEL*)blt_buf,
                                            EfiBltBufferToVideo,
                                            0, 0,
                                            0, 0,
                                            screen_w,
                                            screen_h,
                                            delta);
        if (bs) bs->FreePool(blt_buf);
        if (EFI_ERROR(bltst))
            ERR("[PNG] predecoded BLT failed: %I64x (scaled=%ux%u stride=%u)\n",
                (unsigned long long)(UINTN)bltst,
                tw,
                th,
                (unsigned)delta);
        return !EFI_ERROR(bltst);
    }

    /* Otherwise, decode PNG using libspng to RGBA8 */
    TRACE("[PNG] Rendering asset '%s' (bytes=%lu)\n", asset->name, (unsigned long)asset->size);
    /* quick sanity of malloc/calloc */
    void* t = malloc(4096);
    if (!t) { ERR("[PNG] malloc test failed\n"); }
    else { memset(t, 0, 4096); free(t); TRACE("[PNG] malloc test ok\n"); }
    void* c = calloc(16, 32);
    if (!c) { ERR("[PNG] calloc test failed\n"); }
    else { memset(c, 0xAA, 512); free(c); TRACE("[PNG] calloc test ok\n"); }
    void* c2 = calloc(1, 16384);
    if (!c2) { ERR("[PNG] calloc(16384) failed\n"); }
    else { memset(c2, 0, 16384); free(c2); TRACE("[PNG] calloc(16384) ok\n"); }
    spng_ctx* ctx = spng_ctx_new(0);
    if (!ctx) return FALSE;
    TRACE("[PNG] ctx created\n");
    spng_set_crc_action(ctx, SPNG_CRC_USE, SPNG_CRC_USE);
    int sret = spng_set_png_buffer(ctx, asset->data, asset->size);
    if (sret != 0)
    {
        ERR("[PNG] spng_set_png_buffer failed: %d\n", sret);
        spng_ctx_free(ctx);
        return FALSE;
    }
    TRACE("[PNG] buffer set\n");

    struct spng_ihdr ihdr; 
    int iret = spng_get_ihdr(ctx, &ihdr);
    if (iret != 0)
    {
        ERR("[PNG] spng_get_ihdr failed: %d\n", iret);
        spng_ctx_free(ctx);
        return FALSE;
    }
    TRACE("[PNG] IHDR: %ux%u, depth=%u, color=%u\n",
          (unsigned)ihdr.width, (unsigned)ihdr.height, ihdr.bit_depth, ihdr.color_type);

    size_t out_size = 0;
    int zret = spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &out_size);
    if (zret != 0)
    {
        ERR("[PNG] spng_decoded_image_size failed: %d\n", zret);
        spng_ctx_free(ctx);
        return FALSE;
    }
    TRACE("[PNG] out_size=%lu bytes\n", (unsigned long)out_size);

    EFI_BOOT_SERVICES* bs = display.boot_services;
    if (!bs) { spng_ctx_free(ctx); return FALSE; }

    VOID* rgba_buf = NULL;
    if (!bs || EFI_ERROR(bs->AllocatePool(EfiLoaderData, out_size, &rgba_buf)))
    {
        ERR("[PNG] AllocatePool(%lu) failed\n", (unsigned long)out_size);
        spng_ctx_free(ctx);
        return FALSE;
    }

    int dret = spng_decode_image(ctx, rgba_buf, out_size, SPNG_FMT_RGBA8, 0);
    if (dret != 0)
    {
        ERR("[PNG] spng_decode_image failed: %d\n", dret);
        if (bs) bs->FreePool(rgba_buf);
        spng_ctx_free(ctx);
        return FALSE;
    }
    TRACE("[PNG] decode ok\n");
    spng_ctx_free(ctx);

    unsigned iw = ihdr.width;
    unsigned ih = ihdr.height;
    unsigned dw = iw;
    unsigned dh = ih;

    UiComputeCoverDimensions(iw, ih, screen_w, screen_h, &dw, &dh);
    if (dw == 0 || dh == 0)
    {
        bs->FreePool(rgba_buf);
        return FALSE;
    }

    /* Create BGRA buffer for BLT */
    size_t bgra_size = (size_t)dw * (size_t)dh * 4;
    VOID* bgra_buf = NULL;
    if (!bs || EFI_ERROR(bs->AllocatePool(EfiLoaderData, bgra_size, &bgra_buf)))
    {
        if (bs) bs->FreePool(rgba_buf);
        return FALSE;
    }

    if (dw == iw && dh == ih)
    {
        UiRGBAtoBGRA((const uint8_t*)rgba_buf, (uint8_t*)bgra_buf, iw, ih);
    }
    else
    {
        /* scale up first in RGBA, then convert to BGRA */
        size_t scaled_rgba_size = (size_t)dw * (size_t)dh * 4;
        VOID* scaled_rgba = NULL;
        if (!bs || EFI_ERROR(bs->AllocatePool(EfiLoaderData, scaled_rgba_size, &scaled_rgba)))
        {
            if (bs) bs->FreePool(rgba_buf);
            if (bs) bs->FreePool(bgra_buf);
            return FALSE;
        }
        UiResampleRGBA8((const uint8_t*)rgba_buf, iw, ih, (uint8_t*)scaled_rgba, dw, dh);
        UiRGBAtoBGRA((const uint8_t*)scaled_rgba, (uint8_t*)bgra_buf, dw, dh);
        if (bs) bs->FreePool(scaled_rgba);
    }

    if (bs) bs->FreePool(rgba_buf);

    /* Anchor to the screen origin and copy only the visible portion */
    UINTN delta = (UINTN)dw * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL);

    EFI_STATUS bltst = display.gop->Blt(display.gop,
                                        (EFI_GRAPHICS_OUTPUT_BLT_PIXEL*)bgra_buf,
                                        EfiBltBufferToVideo,
                                        0, 0,
                                        0, 0,
                                        screen_w,
                                        screen_h,
                                        delta);
    if (EFI_ERROR(bltst))
        ERR("[PNG] GOP->Blt failed: %I64x (scaled=%ux%u screen=%ux%u stride=%u)\n",
            (unsigned long long)(UINTN)bltst,
            dw,
            dh,
            screen_w,
            screen_h,
            (unsigned)delta);

    if (bs) bs->FreePool(bgra_buf);
    UiDisplayRelease(&display);
    return !EFI_ERROR(bltst);
}

/* List embedded assets, render main menu, and drive simple page navigation. */
VOID UiSplashListAndWait(VOID)
{
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL* ConIn = GlobalSystemTable ? GlobalSystemTable->ConIn : NULL;
    EFI_BOOT_SERVICES* global_bs = GlobalSystemTable ? GlobalSystemTable->BootServices : NULL;

    extern const EMBEDDED_ASSET gEmbeddedAssets[];
    extern const size_t gEmbeddedAssetCount;

    TRACE("[PNG] Embedded assets count: %lu\n", (unsigned long)gEmbeddedAssetCount);
    for (size_t i = 0; i < gEmbeddedAssetCount; ++i)
    {
        TRACE("[PNG] Asset[%lu]: '%s' size=%lu bytes\n",
              (unsigned long)i, gEmbeddedAssets[i].name,
              (unsigned long)gEmbeddedAssets[i].size);
    }

    const char* chosen = NULL;
    for (size_t i = 0; i < gEmbeddedAssetCount; ++i)
    {
        if (gEmbeddedAssets[i].name && _stricmp(gEmbeddedAssets[i].name, "splash.png") == 0)
        {
            chosen = gEmbeddedAssets[i].name;
            break;
        }
    }
    if (!chosen && gEmbeddedAssetCount > 0)
        chosen = gEmbeddedAssets[0].name;

    if (chosen)
        (void)UiShowPngFullScreen(chosen);
    else
        (void)UiShowPngFullScreen("splash.png");

    UI_DisplayContext display;
    EFI_STATUS st = UiDisplayAcquire(&display);
    if (EFI_ERROR(st))
    {
        TRACE("[PNG] UiDisplayAcquire failed, falling back to legacy wait loop\n");
        while (TRUE)
        {
            if (global_bs)
                global_bs->Stall(1000 * 1000);
        }
    }

    static const char* kPageTitles[4] = {
        "Start ReactOS",
        "Start ReactOS (Debug)",
        "Start ReactOS (Safe Mode)",
        "Open UEFI Shell"
    };

    static const char* kPageBodies[4] = {
        "Launch the standard ReactOS boot sequence.",
        "Boot ReactOS with debugging enabled for troubleshooting.",
        "Boot ReactOS in safe mode with minimal drivers and services.",
        "Exit to the firmware UEFI shell for advanced utilities."
    };

    EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_bg = UiDisplayMakeColor(0x12, 0x12, 0x24);
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL status_fg = UiDisplayMakeColor(0xE0, 0xE0, 0xE0);

    const char* main_status_text = "Press F8 for advanced options";
    const char* advanced_status_text = "ESC to return to main menu";
    const char* static_status_text = "ESC to return to menu";
    const char* log_status_text = "UP/DOWN to scroll - ESC to return";

    UI_MenuPageState menu_state;
    UI_MenuPageState advanced_menu_state;
    UI_LogPageState log_page_state;
    UI_StaticPageState page_states[4];
    UI_MenuItem menu_items[4];
    UI_MenuItem advanced_items[2];
    UI_Page content_pages[4];

    UI_Page menu_page = {
        .id = "main-menu",
        .ops = &UiMenuPageOps,
        .state = &menu_state,
        .initialized = FALSE,
        .constructed = FALSE
    };

    UI_Page advanced_menu_page = {
        .id = "advanced-menu",
        .ops = &UiMenuPageOps,
        .state = &advanced_menu_state,
        .initialized = FALSE,
        .constructed = FALSE
    };

    UI_Page log_page = {
        .id = "boot-log",
        .ops = &UiLogPageOps,
        .state = &log_page_state,
        .initialized = FALSE,
        .constructed = FALSE
    };

    UI_Page* active_page = &menu_page;

    for (size_t i = 0; i < 4; ++i)
    {
        UiStaticPageStateInit(&page_states[i],
                              kPageTitles[i],
                              kPageBodies[i],
                              &menu_page,
                              &active_page,
                              static_status_text,
                              status_bg,
                              status_fg);

        content_pages[i].id = kPageTitles[i];
        content_pages[i].ops = &UiStaticPageOps;
        content_pages[i].state = &page_states[i];
        content_pages[i].initialized = FALSE;
        content_pages[i].constructed = FALSE;

        menu_items[i].label = kPageTitles[i];
        menu_items[i].target_page = &content_pages[i];
    }

    UiMenuPageStateInit(&menu_state,
                        menu_items,
                        4,
                        &active_page,
                        main_status_text,
                        NULL,
                        &advanced_menu_page,
                        status_bg,
                        status_fg);

    advanced_items[0].label = "Open Boot Logs";
    advanced_items[0].target_page = &log_page;
    advanced_items[1].label = "Return to Main Menu";
    advanced_items[1].target_page = &menu_page;

    UiMenuPageStateInit(&advanced_menu_state,
                        advanced_items,
                        2,
                        &active_page,
                        advanced_status_text,
                        &menu_page,
                        NULL,
                        status_bg,
                        status_fg);

    UiLogPageStateInit(&log_page_state,
                       "Boot Log",
                       &advanced_menu_page,
                       &active_page,
                       log_status_text,
                       UiDisplayMakeColor(0x0F, 0x0F, 0x1C),
                       UiDisplayMakeColor(0xFF, 0xFF, 0xFF),
                       UiDisplayMakeColor(0xD6, 0xD6, 0xD6),
                       status_bg,
                       status_fg);

    /* Initial paint */
    UiPageRender(active_page, &display);

    EFI_BOOT_SERVICES* bs = display.boot_services ? display.boot_services : global_bs;

    while (TRUE)
    {
        if (bs && ConIn)
        {
            UINTN idx;
            bs->WaitForEvent(1, &ConIn->WaitForKey, &idx);

            EFI_INPUT_KEY key;
            while (ConIn->ReadKeyStroke(ConIn, &key) == EFI_SUCCESS)
            {
                UI_Page* before = active_page;
                UiPageHandleInput(active_page, &display, key);
                if (active_page != before)
                {
                    UiPageRender(active_page, &display);
                }
                else
                {
                    UiPageRender(active_page, &display);
                }
            }
        }
        else
        {
            if (bs)
                bs->Stall(500 * 1000);
        }
    }
}
