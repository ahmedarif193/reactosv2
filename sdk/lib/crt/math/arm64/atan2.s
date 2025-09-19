
/* INCLUDES ******************************************************************/

#include <asm.inc>

/* CODE **********************************************************************/

    .text

    .global atan2
atan2:
    /* TODO: Implement atan2 for ARM64 */
    /* For now, return 0.0 */
    fmov d0, xzr
    ret

/* EOF */
