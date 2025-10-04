/* Placeholder to create a .symlite section we can overwrite post-link.
 * We allocate a large buffer (64KB) to ensure there's enough space for symbols.
 * The actual size will be adjusted by objcopy during post-build.
 */
#if defined(__GNUC__)
__attribute__((section(".symlite"), used, aligned(16)))
#endif
const char gSymlitePlaceholder[65536] = { 0 };

