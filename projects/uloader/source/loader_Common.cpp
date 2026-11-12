#include <ul/loader/loader_Common.hpp>

namespace ul::loader {

    namespace {

        // The log is truncated on the first write of each process. Without this,
        // a run that crashes before logging anything leaves the previous run's
        // contents in place, making the two runs indistinguishable.
        bool g_LogOpened = false;

    }

    void Log(const char *fmt, ...) {
        char message[512];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(message, sizeof(message), fmt, args);
        va_end(args);

        FILE *f = std::fopen("sdmc:/switch/SwitchU/uloader.log", g_LogOpened ? "a" : "w");
        if (f != nullptr) {
            g_LogOpened = true;
            std::fputs(message, f);
            std::fputc('\n', f);
            std::fclose(f);
        }
    }

    void AbortImpl(const Result rc, const char *expr, const char *file, int line) {
        Log("[uloader] assert %s failed rc=0x%X (%s:%d)", expr, rc, file, line);
        svcExitProcess();
        __builtin_unreachable();
    }

}
