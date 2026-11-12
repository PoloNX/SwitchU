#pragma once
#include <ul/loader/loader_Common.hpp>

#include <cstddef>

namespace ul::loader {

    // This is the launch-parameter storage layout. It must stay byte-for-byte
    // identical to switchu::smi::TargetInput in
    // projects/common/include/switchu/smi_protocol.hpp, which the menu and the
    // daemon use. It is duplicated instead of included so that this vendored
    // GPL-2.0 loader does not depend on SwitchU's GPL-3.0 headers.
    constexpr size_t NroPathSize = 512;
    constexpr size_t NroArgvSize = 2048;
    constexpr size_t MenuCaptionSize = 1024;

    struct TargetInput {
        static constexpr u32 Magic = 0x49444C55; // "ULDI"

        u32 magic;
        bool target_once;
        bool is_auto_game_recording;
        u8 unused[2];
        char nro_path[NroPathSize];
        char nro_argv[NroArgvSize];
        char menu_caption[MenuCaptionSize];
    };
    static_assert(offsetof(TargetInput, magic) == 0);
    static_assert(offsetof(TargetInput, target_once) == 4);
    static_assert(offsetof(TargetInput, is_auto_game_recording) == 5);
    static_assert(offsetof(TargetInput, nro_path) == 8);
    static_assert(offsetof(TargetInput, nro_argv) == 520);
    static_assert(offsetof(TargetInput, menu_caption) == 2568);
    static_assert(sizeof(TargetInput) == 3592);

    // Only produced by uLoader's applet variant when target_once is set.
    struct TargetOutput {
        static constexpr u32 Magic = 0x4F444C55; // "ULDO"

        u32 magic;
        char nro_path[NroPathSize];
        char nro_argv[NroArgvSize];

        template <typename S1, typename S2>
        static inline TargetOutput Create(const S1 &nro_path, const S2 &nro_argv) {
            TargetOutput target_opt = {
                .magic = Magic
            };
            CopyToStringBuffer(target_opt.nro_path, nro_path);
            CopyToStringBuffer(target_opt.nro_argv, nro_argv);
            return target_opt;
        }

        inline bool IsValid() const {
            return this->magic == Magic;
        }
    };

}
