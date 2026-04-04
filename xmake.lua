-- ──────────────────────────────────────────────────────────────────────────────
-- SwitchU — xmake build
-- ──────────────────────────────────────────────────────────────────────────────
set_xmakever("2.8.0")
set_project("SwitchU")

add_repositories("switch-repo https://github.com/PoloNX/switch-repo.git")

includes("toolchain/*.lua")
add_rules("mode.debug", "mode.release")

add_requires("libsdl", "libsdl_mixer", "libsdl_ttf", "zlib", "libwebp", {configs = {toolchains = "devkita64"}})
if get_config("backend") ~= "sdl2" then
    add_requires("deko3d", {configs = {toolchains = "devkita64"}})
end

-- ── Options ──────────────────────────────────────────────────────────────────
option("homebrew")
    set_default(false)
    set_showmenu(true)
    set_description("Build .nro homebrew instead of .nsp for LayeredFS")
option_end()

option("backend")
    set_default("deko3d")
    set_showmenu(true)
    set_description("GPU rendering backend: deko3d or sdl2")
    set_values("deko3d", "sdl2")
option_end()

-- ── Static library: nxui ─────────────────────────────────────────────────────
target("nxui")
    set_kind("static")
    if not is_plat("cross") then return end

    set_toolchains("devkita64")
    set_languages("c++20")

    -- Backend-independent sources
    add_files("lib/nxui/src/core/Application.cpp")
    add_files("lib/nxui/src/core/Animation.cpp")
    add_files("lib/nxui/src/core/I18n.cpp")
    add_files("lib/nxui/src/core/Input.cpp")
    add_files("lib/nxui/src/core/Theme.cpp")
    add_files("lib/nxui/src/widgets/*.cpp")
    add_files("lib/nxui/src/focus/*.cpp")

    -- Backend-specific sources
    if get_config("backend") == "sdl2" then
        add_defines("NXUI_BACKEND_SDL2", {public = true})
        add_files("lib/nxui/src/core/GpuDevice_sdl2.cpp")
        add_files("lib/nxui/src/core/Renderer_sdl2.cpp")
        add_files("lib/nxui/src/core/Texture_sdl2.cpp")
        add_files("lib/nxui/src/core/Font.cpp")   -- Font is backend-agnostic
    else
        add_defines("NXUI_BACKEND_DEKO3D", {public = true})
        add_files("lib/nxui/src/core/GpuDevice.cpp")
        add_files("lib/nxui/src/core/Renderer.cpp")
        add_files("lib/nxui/src/core/Texture.cpp")
        add_files("lib/nxui/src/core/Font.cpp")
    end

    add_includedirs("lib/nxui/include", {public = true})
    add_includedirs("lib/nxui/include/nxui/third_party/stb")

    add_cxxflags("-fno-rtti", "-fexceptions", {force = true})
    if get_config("backend") == "sdl2" then
        add_packages("libsdl", "libsdl_ttf", "libwebp")
    else
        add_packages("deko3d", "libsdl", "libsdl_ttf", "libwebp")
    end

    if is_mode("release") then
        add_cxflags("-O3", "-flto=auto", "-ffast-math", {force = true})
    end

    on_install(function(target) end)
target_end()

-- ── Static library: libnxtc ──────────────────────────────────────────────────
target("nxtc")
    set_kind("static")
    if not is_plat("cross") then return end

    set_toolchains("devkita64")

    add_files("lib/libnxtc/source/*.c")
    add_includedirs("lib/libnxtc/include", {public = true})
    add_packages("zlib")

    if is_mode("release") then
        add_cflags("-O2", {force = true})
    end

    on_install(function(target) end)
target_end()


-- ── Homebrew (monolithic NRO) — original single-binary build ─────────────────
target("SwitchU")
    set_kind("binary")
    if not is_plat("cross") then return end
    if not has_config("homebrew") then
        -- Skip this target entirely in non-homebrew (two-applet) mode
        set_default(false)
    end

    set_toolchains("devkita64")
    set_languages("c++20")
    add_rules("switch")

    add_deps("nxui", "nxtc")

    add_files("src/*.cpp")
    add_files("projects/menu/src/core/*.cpp")
    add_files("projects/menu/src/widgets/*.cpp")
    add_files("projects/menu/src/settings/*.cpp")
    add_files("projects/menu/src/settings/tabs/*.cpp")
    add_files("projects/menu/src/sidebar/*.cpp")
    add_files("projects/menu/src/launcher/*.cpp")
    add_files("projects/menu/src/bluetooth/*.cpp")

    add_includedirs("projects/common/include", {public = false})
    add_includedirs("projects/menu/src", {public = false})

    add_cxxflags("-fno-rtti", "-fexceptions", {force = true})
    if get_config("backend") == "sdl2" then
        add_packages("libsdl", "libsdl_mixer", "libsdl_ttf", "zlib", "libwebp")
    else
        add_packages("deko3d", "libsdl", "libsdl_mixer", "libsdl_ttf", "zlib", "libwebp")
    end
    add_syslinks("nx")

    if is_mode("release") then
        add_cxflags("-O3", "-flto=auto", "-ffast-math", {force = true})
        add_ldflags("-flto=auto", {force = true})
    end

    add_defines("SWITCHU_HOMEBREW")
    add_defines('SWITCHU_VERSION="1.0.1"')
    set_values("switch.name",    "SwitchU")
    set_values("switch.author",  "PoloNX")
    set_values("switch.version", "1.0.1")
    set_values("switch.romfs",   "romfs")
    set_values("switch.tid",     "0100000000001000")
    set_values("switch.json",    "SwitchU.json")
    set_values("switch.format",  "nro")
target_end()

-- ══════════════════════════════════════════════════════════════════════════════
-- Two-applet mode (non-homebrew):  daemon  +  menu
-- ══════════════════════════════════════════════════════════════════════════════

-- ── Daemon (system applet, qlaunch replacement) ──────────────────────────────
target("switchu-daemon")
    set_kind("binary")
    if not is_plat("cross") then return end
    if has_config("homebrew") then set_default(false) end

    set_toolchains("devkita64")
    set_languages("c++20")
    add_rules("switch")

    add_deps("nxtc")

    add_files("projects/daemon/src/main.cpp")

    add_includedirs("projects/common/include", {public = false})
    add_includedirs("lib/libnxtc/include")

    add_cxxflags("-fno-rtti", "-fexceptions", {force = true})
    add_packages("zlib")
    add_syslinks("nx")

    if is_mode("release") then
        add_cxflags("-O3", "-flto=auto", "-ffast-math", {force = true})
        add_ldflags("-flto=auto", {force = true})
    end

    set_values("switch.name",    "switchu-daemon")
    set_values("switch.author",  "PoloNX")
    set_values("switch.version", "1.0.1")
    -- Daemon has no GPU, no romfs, no SD assets.
    set_values("switch.tid",     "0100000000001000")
    set_values("switch.json",    "projects/daemon/daemon.json")
    set_values("switch.format",  "nsp")
target_end()

-- ── Menu (library applet, hijacking eShop) ───────────────────────────────────
target("switchu-menu")
    set_kind("binary")
    if not is_plat("cross") then return end
    if has_config("homebrew") then set_default(false) end

    set_toolchains("devkita64")
    set_languages("c++20")
    add_rules("switch")

    add_deps("nxui", "nxtc")

    add_files("projects/menu/src/main.cpp")
    add_files("projects/menu/src/core/*.cpp")
    add_files("projects/menu/src/widgets/*.cpp")
    add_files("projects/menu/src/settings/*.cpp")
    add_files("projects/menu/src/settings/tabs/*.cpp")
    add_files("projects/menu/src/sidebar/*.cpp")
    add_files("projects/menu/src/launcher/*.cpp")
    add_files("projects/menu/src/bluetooth/*.cpp")

    add_includedirs("projects/common/include",  {public = false})
    add_includedirs("projects/menu/src",         {public = false})
    add_includedirs("lib/libnxtc/include",       {public = false})

    add_cxxflags("-fno-rtti", "-fexceptions", {force = true})
    if get_config("backend") == "sdl2" then
        add_packages("libsdl", "libsdl_mixer", "libsdl_ttf", "zlib", "libwebp")
    else
        add_packages("deko3d", "libsdl", "libsdl_mixer", "libsdl_ttf", "zlib", "libwebp")
    end
    add_syslinks("nx")

    add_defines("SWITCHU_MENU")
    add_defines('SWITCHU_VERSION="1.0.1"')

    if is_mode("release") then
        add_cxflags("-O3", "-flto=auto", "-ffast-math", {force = true})
        add_ldflags("-flto=auto", {force = true})
    end

    set_values("switch.name",    "switchu-menu")
    set_values("switch.author",  "PoloNX")
    set_values("switch.version", "1.0.1")
    set_values("switch.romfs",   "romfs")
    set_values("switch.tid",     "010000000000100B")
    set_values("switch.json",    "projects/menu/menu.json")
    set_values("switch.format",  "nsp")
    set_values("switch.assets_dir", "SwitchU")   -- SD assets → sdmc:/switch/SwitchU/
target_end()
