/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     C Runtime stubs for rosload
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 */

#include <freeldr.h>

/* Global variables from first-stage loader */
UCHAR FrldrBootDrive = 0x80;  /* Default to first hard disk */
ULONG FrldrBootPartition = 0;
CCHAR FrLdrBootPath[MAX_PATH] = "";

/* String functions */
int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--)
    {
        if (*p1 != *p2)
            return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

char *_strupr(char *str)
{
    char *p = str;
    while (*p)
    {
        if (*p >= 'a' && *p <= 'z')
            *p -= 'a' - 'A';
        p++;
    }
    return str;
}

/* Number conversion functions */
unsigned long strtoul(const char *nptr, char **endptr, int base)
{
    unsigned long result = 0;
    const char *p = nptr;

    /* Skip whitespace */
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;

    /* Handle base detection */
    if (base == 0)
    {
        if (*p == '0')
        {
            p++;
            if (*p == 'x' || *p == 'X')
            {
                base = 16;
                p++;
            }
            else
            {
                base = 8;
            }
        }
        else
        {
            base = 10;
        }
    }
    else if (base == 16 && *p == '0' && (p[1] == 'x' || p[1] == 'X'))
    {
        p += 2;
    }

    /* Convert digits */
    while (*p)
    {
        int digit;
        if (*p >= '0' && *p <= '9')
            digit = *p - '0';
        else if (*p >= 'a' && *p <= 'z')
            digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z')
            digit = *p - 'A' + 10;
        else
            break;

        if (digit >= base)
            break;

        result = result * base + digit;
        p++;
    }

    if (endptr)
        *endptr = (char *)p;

    return result;
}

unsigned long long strtoull(const char *nptr, char **endptr, int base)
{
    unsigned long long result = 0;
    const char *p = nptr;

    /* Skip whitespace */
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;

    /* Handle base detection */
    if (base == 0)
    {
        if (*p == '0')
        {
            p++;
            if (*p == 'x' || *p == 'X')
            {
                base = 16;
                p++;
            }
            else
            {
                base = 8;
            }
        }
        else
        {
            base = 10;
        }
    }
    else if (base == 16 && *p == '0' && (p[1] == 'x' || p[1] == 'X'))
    {
        p += 2;
    }

    /* Convert digits */
    while (*p)
    {
        int digit;
        if (*p >= '0' && *p <= '9')
            digit = *p - '0';
        else if (*p >= 'a' && *p <= 'z')
            digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z')
            digit = *p - 'A' + 10;
        else
            break;

        if (digit >= base)
            break;

        result = result * base + digit;
        p++;
    }

    if (endptr)
        *endptr = (char *)p;

    return result;
}

/* sprintf implementation - simplified version */
int sprintf(char *str, const char *format, ...)
{
    va_list ap;
    int ret;

    va_start(ap, format);
    ret = vsprintf(str, format, ap);
    va_end(ap);

    return ret;
}

/* vsprintf implementation - simplified version */
int vsprintf(char *str, const char *format, va_list ap)
{
    char *dst = str;
    const char *fmt = format;

    while (*fmt)
    {
        if (*fmt != '%')
        {
            *dst++ = *fmt++;
            continue;
        }

        fmt++; /* Skip '%' */

        /* Handle format specifiers */
        switch (*fmt)
        {
            case 'd':
            case 'i':
            {
                int val = va_arg(ap, int);
                char buffer[32];
                int i = 0;
                int negative = 0;

                if (val < 0)
                {
                    negative = 1;
                    val = -val;
                }

                if (val == 0)
                    buffer[i++] = '0';
                else
                {
                    while (val > 0)
                    {
                        buffer[i++] = '0' + (val % 10);
                        val /= 10;
                    }
                }

                if (negative)
                    *dst++ = '-';

                while (i > 0)
                    *dst++ = buffer[--i];

                fmt++;
                break;
            }

            case 'u':
            {
                unsigned int val = va_arg(ap, unsigned int);
                char buffer[32];
                int i = 0;

                if (val == 0)
                    buffer[i++] = '0';
                else
                {
                    while (val > 0)
                    {
                        buffer[i++] = '0' + (val % 10);
                        val /= 10;
                    }
                }

                while (i > 0)
                    *dst++ = buffer[--i];

                fmt++;
                break;
            }

            case 'x':
            case 'X':
            {
                unsigned int val = va_arg(ap, unsigned int);
                char buffer[32];
                int i = 0;
                int uppercase = (*fmt == 'X');

                if (val == 0)
                    buffer[i++] = '0';
                else
                {
                    while (val > 0)
                    {
                        int digit = val % 16;
                        if (digit < 10)
                            buffer[i++] = '0' + digit;
                        else if (uppercase)
                            buffer[i++] = 'A' + (digit - 10);
                        else
                            buffer[i++] = 'a' + (digit - 10);
                        val /= 16;
                    }
                }

                while (i > 0)
                    *dst++ = buffer[--i];

                fmt++;
                break;
            }

            case 's':
            {
                char *s = va_arg(ap, char *);
                if (!s) s = "(null)";
                while (*s)
                    *dst++ = *s++;
                fmt++;
                break;
            }

            case 'c':
            {
                *dst++ = (char)va_arg(ap, int);
                fmt++;
                break;
            }

            case '%':
                *dst++ = '%';
                fmt++;
                break;

            default:
                /* Unknown format, just copy */
                *dst++ = '%';
                *dst++ = *fmt++;
                break;
        }
    }

    *dst = '\0';
    return (int)(dst - str);
}

/* sscanf - simplified implementation for common use cases */
int sscanf(const char *str, const char *format, ...)
{
    va_list ap;
    int matched = 0;
    const char *s = str;
    const char *fmt = format;

    va_start(ap, format);

    while (*fmt)
    {
        if (*fmt == '%')
        {
            fmt++;
            if (*fmt == 'u' || *fmt == 'd')
            {
                unsigned int *pval = va_arg(ap, unsigned int*);
                unsigned int val = 0;

                /* Skip whitespace */
                while (*s == ' ' || *s == '\t') s++;

                /* Parse number */
                while (*s >= '0' && *s <= '9')
                {
                    val = val * 10 + (*s - '0');
                    s++;
                }

                *pval = val;
                matched++;
                fmt++;
            }
            else if (*fmt == 's')
            {
                char *dst = va_arg(ap, char*);

                /* Skip whitespace */
                while (*s == ' ' || *s == '\t') s++;

                /* Copy non-whitespace */
                while (*s && *s != ' ' && *s != '\t')
                {
                    *dst++ = *s++;
                }
                *dst = '\0';

                matched++;
                fmt++;
            }
            else
            {
                fmt++;
            }
        }
        else
        {
            if (*fmt == *s)
            {
                fmt++;
                s++;
            }
            else
            {
                break;
            }
        }
    }

    va_end(ap);
    return matched;
}

/* atol implementation */
long atol(const char *str)
{
    long result = 0;
    int sign = 1;

    /* Skip whitespace */
    while (*str == ' ' || *str == '\t') str++;

    /* Handle sign */
    if (*str == '-')
    {
        sign = -1;
        str++;
    }
    else if (*str == '+')
    {
        str++;
    }

    /* Convert digits */
    while (*str >= '0' && *str <= '9')
    {
        result = result * 10 + (*str - '0');
        str++;
    }

    return sign * result;
}

/* strncpy implementation */
char *strncpy(char *dest, const char *src, size_t n)
{
    char *d = dest;
    const char *s = src;

    /* Copy up to n characters */
    while (n > 0 && *s)
    {
        *d++ = *s++;
        n--;
    }

    /* Pad with zeros if necessary */
    while (n > 0)
    {
        *d++ = '\0';
        n--;
    }

    return dest;
}

/* strcmp implementation */
int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

/* BIOS Int386 stub - rosload doesn't use BIOS interrupts */
int __cdecl Int386(int ivec, REGS *in_regs, REGS *out_regs)
{
    /* rosload runs under UEFI/Windows, not BIOS */
    return 0;
}

/* DbgBreakPoint stub */
void DbgBreakPoint(void)
{
    /* In rosload, we just return */
    return;
}

/* PnpBios stubs - rosload doesn't use PnP BIOS */
ULONG_PTR __cdecl PnpBiosSupported(VOID)
{
    return 0; /* PnP BIOS not supported in rosload */
}

ULONG __cdecl PnpBiosGetDeviceNodeCount(ULONG *NodeSize, ULONG *NodeCount)
{
    *NodeCount = 0;
    *NodeSize = 0;
    return 0x82; /* Function not supported */
}

ULONG __cdecl PnpBiosGetDeviceNode(UCHAR *NodeId, UCHAR *NodeBuffer)
{
    return 0x82; /* Function not supported */
}

ULONG __cdecl PnpBiosGetDockStationInformation(UCHAR *DockingStationInfo)
{
    return 0x82; /* Function not supported */
}