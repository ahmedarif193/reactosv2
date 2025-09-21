#include <stddef.h>

#if defined(__clang__) && (defined(_M_ARM64) || defined(__aarch64__))
double fabs(double value)
{
    return (value < 0.0) ? -value : value;
}

float fabsf(float value)
{
    return (value < 0.0f) ? -value : value;
}

long double fabsl(long double value)
{
    return (value < 0.0L) ? -value : value;
}
#endif
