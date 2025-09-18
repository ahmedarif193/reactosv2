
/* INCLUDES ******************************************************************/

/* We need one of these first! */
/* #include <kxarm64.h> */

/* CODE **********************************************************************/
    .text
    .align 2
    .global __chkstk
__chkstk:
    /* Stack probing stub for ARM64: nothing required for GCC/Mingw */
    ret

    .global __alloca_probe
__alloca_probe:
    /* Stack probing stub for ARM64: nothing required for GCC/Mingw */
    ret
/* EOF */
