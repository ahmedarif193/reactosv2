/* Minimal stdio stubs to satisfy libspng static references; never used */
#include <stddef.h>

typedef void FILE;

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream)
{
    (void)ptr; (void)size; (void)nmemb; (void)stream; return 0;
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream)
{
    (void)ptr; (void)size; (void)nmemb; (void)stream; return 0;
}

int feof(FILE* stream)
{
    (void)stream; return 1;
}

