#pragma once

namespace Log {
    void init();
    void shutdown();
    void log(const char* fmt, ...);
    void warn(const char* fmt, ...);
    void error(const char* fmt, ...);
    void debug(const char* fmt, ...);
    void info(const char* fmt, ...);
}
