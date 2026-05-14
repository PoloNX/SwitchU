set_project("SwitchU")

add_repositories("switch-repo https://github.com/PoloNX/switch-repo.git")

includes("toolchain/*.lua")
add_rules("mode.debug", "mode.release")

local version = "1.0.1"
local version_define = string.format('SWITCHU_VERSION="%s"', version)

set_version(version)

add_requires("libsdl", "libsdl_mixer", "libsdl_ttf", "zlib", "libwebp", "nlohmann_json", "fmt", "libcurl", "curlpp", {configs = {toolchains = "devkita64"}})
if get_config("backend") ~= "sdl2" then
    add_requires("deko3d", {configs = {toolchains = "devkita64"}})
    if is_mode("debug") then
        add_requires("imgui", {configs = {toolchains = "devkita64"}})
    end
end

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

target("nxui")
    set_kind("static")
    set_default(false)
    if not is_plat("cross") then return end

    set_toolchains("devkita64")
    set_languages("c++20")

    add_files("lib/nxui/src/core/Application.cpp")
    add_files("lib/nxui/src/core/Animation.cpp")
    add_files("lib/nxui/src/core/I18n.cpp")
    add_files("lib/nxui/src/core/Input.cpp")
    add_files("lib/nxui/src/core/Theme.cpp")
    add_files("lib/nxui/src/widgets/*.cpp")
    add_files("lib/nxui/src/focus/*.cpp")
    add_files("lib/nxui/src/core/Font.cpp")

    if get_config("backend") == "sdl2" then
        add_defines("NXUI_BACKEND_SDL2", {public = true})
        add_files("lib/nxui/src/core/GpuDevice_sdl2.cpp")
        add_files("lib/nxui/src/core/Renderer_sdl2.cpp")
        add_files("lib/nxui/src/core/Texture_sdl2.cpp")
    else
        add_defines("NXUI_BACKEND_DEKO3D", {public = true})
        add_files("lib/nxui/src/core/GpuDevice.cpp")
        add_files("lib/nxui/src/core/Renderer.cpp")
        add_files("lib/nxui/src/core/Texture.cpp")
    end

    add_includedirs("lib/nxui/include", {public = true})
    add_includedirs("lib/nxui/include/nxui/third_party/stb")

    add_packages("libsdl", "libsdl_ttf", "libwebp")

    add_cxxflags("-frtti", "-fexceptions", {force = true})
    if get_config("backend") == "deko3d" then
        add_packages("deko3d")
    end

    if is_mode("release") then
        add_cxflags("-O3", "-flto=auto", "-ffast-math", {force = true})
    end
target_end()

target("nxtc")
    set_kind("static")
    set_default(false)
    if not is_plat("cross") then return end

    set_toolchains("devkita64")

    add_files("lib/libnxtc/source/*.c")
    add_includedirs("lib/libnxtc/include", {public = true})
    add_packages("zlib")

    if is_mode("release") then
        add_cflags("-O2", {force = true})
    end
target_end()

target("SwitchU")
    set_kind("binary")
    if not is_plat("cross") then return end

    set_toolchains("devkita64")
    set_languages("c++20")
    add_rules("switch")

    add_deps("nxui", "nxtc")
    add_includedirs("projects/common/include", {public = false})
    add_includedirs("projects/menu/src", {public = false})
    add_files("projects/menu/src/**.cpp")
    add_packages("nlohmann_json", "fmt", "libsdl", "libsdl_mixer", "libsdl_ttf", "zlib", "libwebp", "libcurl", "curlpp")

    if is_mode("debug") and get_config("backend") ~= "sdl2" then
        add_packages("imgui")
        add_defines("SWITCHU_DEBUG_UI")
    end

    add_cxxflags("-frtti", "-fexceptions", {force = true})
    if get_config("backend") == "deko3d" then
        add_packages("deko3d")
    end
    add_syslinks("nx")

    if is_mode("release") then
        add_cxflags("-O3", "-flto=auto", "-ffast-math", {force = true})
        add_ldflags("-flto=auto", {force = true})
    end

    add_defines(version_define)

    if has_config("homebrew") then
        add_defines("SWITCHU_HOMEBREW")
        set_values("switch.name",    "SwitchU")
        set_values("switch.author",  "PoloNX")
        set_values("switch.version", version)
        set_values("switch.romfs",   "romfs")
        set_values("switch.tid",     "0100000000001000")
        set_values("switch.json",    "SwitchU.json")
        set_values("switch.format",  "nro")
    else
        -- experiment/standalone-menu: no daemon, menu IS the system applet
        add_defines("SWITCHU_MENU")
        add_defines("SWITCHU_STANDALONE")
        set_values("switch.name",    "SwitchU")
        set_values("switch.author",  "PoloNX")
        set_values("switch.version", version)
        set_values("switch.romfs",   "romfs")
        set_values("switch.tid",     "0100000000001000")
        set_values("switch.json",    "projects/menu/standalone.json")
        set_values("switch.format",  "nsp")
        set_values("switch.assets_dir", "SwitchU")
    end
target_end()

target("switchu-daemon")
    set_kind("binary")
    if not is_plat("cross") then return end
    -- experiment/standalone-menu: daemon is disabled; the standalone menu replaces it.
    set_default(false)

    set_toolchains("devkita64")
    set_languages("c++20")
    add_rules("switch")

    add_deps("nxtc")

    add_files("projects/daemon/src/**.cpp")

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
    set_values("switch.version", version)
    set_values("switch.tid",     "0100000000001000")
    set_values("switch.json",    "projects/daemon/daemon.json")
    set_values("switch.format",  "nsp")
target_end()

