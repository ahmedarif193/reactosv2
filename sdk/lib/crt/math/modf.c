/* @(#)s_modf.c 1.3 95/01/18 */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */
#define modfl _dummy_modfl
#include <precomp.h>
#undef modfl

//static const double one = 1.0;



long double modfl(long double __x, long double *__i)
{
    /* Handle NaNs and infinities explicitly */
    if (_isnanl(__x))
    {
        *__i = __x;
        return __x;
    }
    if (_isinfl(__x))
    {
        *__i = __x;
        return (__x < 0.0L) ? -0.0L : 0.0L;
    }

    /* |x| < 1: integer part is signed zero */
    if (__x > -1.0L && __x < 1.0L)
    {
        *__i = (__x < 0.0L) ? -0.0L : 0.0L;
        return __x;
    }

    /* Truncate toward zero using floor/ceil depending on the sign */
    long double ip = (__x >= 0.0L) ? floorl(__x) : ceill(__x);
    *__i = ip;
    return __x - ip;
}
