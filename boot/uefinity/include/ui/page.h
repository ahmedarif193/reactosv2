#pragma once

#include <uefildr.h>

#include "ui/display.h"

typedef struct UI_Page UI_Page;

typedef EFI_STATUS (*UI_PageInitFn)(UI_Page* page, const UI_DisplayContext* ctx);
typedef EFI_STATUS (*UI_PageConstructFn)(UI_Page* page, const UI_DisplayContext* ctx);
typedef EFI_STATUS (*UI_PageRenderFn)(UI_Page* page, const UI_DisplayContext* ctx);
typedef EFI_STATUS (*UI_PageHandleInputFn)(UI_Page* page, const UI_DisplayContext* ctx, EFI_INPUT_KEY key);

typedef struct UI_PageOps {
    UI_PageInitFn Init;
    UI_PageConstructFn Construct;
    UI_PageRenderFn Render;
    UI_PageHandleInputFn HandleInput;
} UI_PageOps;

struct UI_Page {
    const char* id;
    const UI_PageOps* ops;
    VOID* state;
    BOOLEAN initialized;
    BOOLEAN constructed;
};

EFI_STATUS UiPageInit(UI_Page* page, const UI_DisplayContext* ctx);
EFI_STATUS UiPageConstruct(UI_Page* page, const UI_DisplayContext* ctx);
EFI_STATUS UiPageRender(UI_Page* page, const UI_DisplayContext* ctx);
EFI_STATUS UiPageHandleInput(UI_Page* page, const UI_DisplayContext* ctx, EFI_INPUT_KEY key);

