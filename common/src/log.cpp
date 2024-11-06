#include "log.hpp"

#include <stdarg.h>
#include <stdio.h>

#define ENABLE_STDOUT 1

namespace Log {
    static FILE* log_file;

    void init() {
        log_file = fopen("log.txt", "w");
    }
    void shutdown() {
        fclose(log_file);
    }
    void log(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vfprintf(log_file, fmt, args);
        fflush(log_file);
        va_end(args);

#if ENABLE_STDOUT
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
#endif
    }
    void _tagged_log(const char* tag, const char* fmt, va_list args) {
        log("[%s] ", tag);
        vfprintf(log_file, fmt, args);
        fprintf(log_file, "\n");

#if ENABLE_STDOUT
        vprintf(fmt, args);
        printf("\n");
#endif
    }
    void warn(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        _tagged_log("WARN ", fmt, args);
        va_end(args);
    }
    void error(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        _tagged_log("ERROR", fmt, args);
        va_end(args);
    }
    void debug(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        _tagged_log("DEBUG", fmt, args);
        va_end(args);
    }
    void info(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        _tagged_log("INFO ", fmt, args);
        va_end(args);
    }
}
