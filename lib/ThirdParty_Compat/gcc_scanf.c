#include <stdio.h>
#include <stdarg.h>

extern int __vfscanf(FILE *stream, const char *format, va_list ap);
extern int __vsscanf(const char *str, const char *format, va_list ap);

int __isoc99_vfscanf(FILE *stream, const char *format, va_list ap)
{
    return __vfscanf(stream, format, ap);
}

int __isoc99_fscanf(FILE *stream, const char *format, ...)
{
    va_list ap;
    int ret;

    va_start(ap, format);
    ret = __vfscanf(stream, format, ap);
    va_end(ap);

    return ret;
}

int __isoc99_vsscanf(const char *str, const char *format, va_list ap)
{
    return __vsscanf(str, format, ap);
}

int __isoc99_sscanf(const char *str, const char *format, ...)
{
    va_list ap;
    int ret;

    va_start(ap, format);
    ret = __vsscanf(str, format, ap);
    va_end(ap);

    return ret;
}
