#pragma once
#include <switch.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <utility>

namespace ul::loader {

    // ResultSuccess is not defined by libnx; uLoader only needs zero.
    inline constexpr Result ResultSuccess = 0;
    inline constexpr Result ResultInvalidProcessType = MAKERESULT(Module_Libnx, 0x2C0);
    inline constexpr Result ResultInvalidTargetInputMagic = MAKERESULT(Module_Libnx, 0x2C1);
    inline constexpr Result ResultInvalidTargetInputSize = MAKERESULT(Module_Libnx, 0x2C2);
    inline constexpr Result ResultAllocateHeapFailure = MAKERESULT(Module_Libnx, 0x2C3);
    inline constexpr Result ResultNroOpenFailure = MAKERESULT(Module_Libnx, 0x2C4);
    inline constexpr Result ResultNroHeaderReadFailure = MAKERESULT(Module_Libnx, 0x2C5);
    inline constexpr Result ResultNroReadFailure = MAKERESULT(Module_Libnx, 0x2C6);
    inline constexpr Result ResultInvalidNroMagic = MAKERESULT(Module_Libnx, 0x2C7);
    inline constexpr Result ResultInvalidNroSegments = MAKERESULT(Module_Libnx, 0x2C8);
    inline constexpr Result ResultMapCodeMemoryFailure = MAKERESULT(Module_Libnx, 0x2C9);
    inline constexpr Result ResultMapTextFailure = MAKERESULT(Module_Libnx, 0x2CA);
    inline constexpr Result ResultMapRodataFailure = MAKERESULT(Module_Libnx, 0x2CB);
    inline constexpr Result ResultMapDataBssFailure = MAKERESULT(Module_Libnx, 0x2CC);
    inline constexpr Result ResultUnmapTextFailure = MAKERESULT(Module_Libnx, 0x2CD);
    inline constexpr Result ResultUnmapRodataFailure = MAKERESULT(Module_Libnx, 0x2CE);
    inline constexpr Result ResultUnmapDataBssFailure = MAKERESULT(Module_Libnx, 0x2CF);

    void Log(const char *fmt, ...);

    NX_NORETURN void AbortImpl(Result rc, const char *expr, const char *file, int line);

    template <size_t N>
    inline bool CopyToStringBuffer(char (&dst)[N], std::string_view src) noexcept {
        static_assert(N > 0, "Destination buffer must have size > 0");

        const size_t copy_len = src.size() < (N - 1) ? src.size() : (N - 1);
        std::memcpy(dst, src.data(), copy_len);
        dst[copy_len] = '\0';

        return copy_len < src.size(); // true if truncated
    }

    template <typename Fn>
    class ScopeExit {
    public:
        explicit ScopeExit(Fn fn) : m_fn(std::move(fn)) {}
        ~ScopeExit() { m_fn(); }

        ScopeExit(const ScopeExit &) = delete;
        ScopeExit &operator =(const ScopeExit &) = delete;

    private:
        Fn m_fn;
    };

    template <typename Fn>
    inline ScopeExit<Fn> MakeScopeExit(Fn fn) {
        return ScopeExit<Fn>(std::move(fn));
    }

}

#define UL_CONCAT_IMPL(x, y) x##y
#define UL_CONCAT(x, y) UL_CONCAT_IMPL(x, y)
#define UL_UNIQUE_VAR_NAME(prefix) UL_CONCAT(prefix##_, __COUNTER__)

#define UL_ON_SCOPE_EXIT(...) \
    auto UL_UNIQUE_VAR_NAME(ul_scope_exit) = ::ul::loader::MakeScopeExit([&]() { __VA_ARGS__; });

#define UL_RC_TRY(res_expr) do { \
    const Result _ul_rc = (res_expr); \
    if (R_FAILED(_ul_rc)) { \
        return _ul_rc; \
    } \
} while (0)

#define UL_RC_ASSERT(res_expr) do { \
    const Result _ul_rc = (res_expr); \
    if (R_FAILED(_ul_rc)) { \
        ::ul::loader::AbortImpl(_ul_rc, #res_expr, __FILE__, __LINE__); \
    } \
} while (0)
