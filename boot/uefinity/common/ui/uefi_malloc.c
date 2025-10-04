/* Minimal malloc/calloc/realloc/free for UEFI Boot Services pool with size header */
#include <uefildr.h>
#include <string.h>

extern EFI_SYSTEM_TABLE* GlobalSystemTable;

typedef struct _UEFI_HEAP_HDR {
    size_t size;
} UEFI_HEAP_HDR;

static inline UEFI_HEAP_HDR* hdr_from_user(void* user)
{
    return (UEFI_HEAP_HDR*)((char*)user - sizeof(UEFI_HEAP_HDR));
}

void* malloc(size_t size)
{
    if (!GlobalSystemTable || !GlobalSystemTable->BootServices || size == 0) return NULL;
    VOID* raw = NULL;
    size_t total = size + sizeof(UEFI_HEAP_HDR);
    if (EFI_ERROR(GlobalSystemTable->BootServices->AllocatePool(EfiLoaderData, total, &raw))) return NULL;
    UEFI_HEAP_HDR* h = (UEFI_HEAP_HDR*)raw;
    h->size = size;
    return (void*)((char*)raw + sizeof(UEFI_HEAP_HDR));
}

void free(void* ptr)
{
    if (!ptr) return;
    if (!GlobalSystemTable || !GlobalSystemTable->BootServices) return;
    VOID* raw = (VOID*)hdr_from_user(ptr);
    (void)GlobalSystemTable->BootServices->FreePool(raw);
}

void* calloc(size_t nmemb, size_t size)
{
    size_t total;
    if (__builtin_mul_overflow(nmemb, size, &total)) return NULL;
    void* p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void* realloc(void* ptr, size_t size)
{
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }
    UEFI_HEAP_HDR* h = hdr_from_user(ptr);
    size_t old = h->size;
    void* np = malloc(size);
    if (!np) return NULL;
    size_t to_copy = old < size ? old : size;
    memcpy(np, ptr, to_copy);
    free(ptr);
    return np;
}
