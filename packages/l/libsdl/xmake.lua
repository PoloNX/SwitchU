package("libsdl")
    set_homepage("https://github.com/devkitPro/SDL")
    set_description("SDL2 for Nintendo Switch using the audout audio backend")
    set_license("Zlib")

    add_deps("mesa", "libnx")

    on_fetch(function(package)
        local package_name = "switch-sdl2-2.28.5-3-any.pkg.tar.zst"
        local package_url = "https://wii.leseratte10.de/devkitPro/switch/sdl2/" .. package_name
        local package_sha256 = "b554bde32201f32f93a5be1a6561cf2abb9fd7755e00ebbce8a692b89cf0646e"
        local root = path.join(os.projectdir(), "build", "sdl2-audout")
        local package_path = path.join(root, package_name)
        local archive = path.join(root, "lib", "libSDL2.a")
        local devkitpro = os.getenv("DEVKITPRO") or "/opt/devkitpro"
        local nm = path.join(devkitpro, "devkitA64", "bin", "aarch64-none-elf-nm")

        os.mkdir(root)
        if not os.isfile(package_path) then
            cprint("${color.build.target}downloading${clear} SDL2 audout")
            os.execv("curl", {"-fL", package_url, "-o", package_path})
        end
        if hash.sha256(package_path) ~= package_sha256 then
            raise("unexpected checksum for " .. package_name)
        end
        if not os.isfile(archive) then
            cprint("${color.build.target}extracting${clear} SDL2 audout")
            os.execv("tar", {"--zstd", "-xf", package_path, "--strip-components=4", "-C", root})
        end
        os.execv("sh", {
            "-c",
            'symbols="$($1 -g "$2")" && printf "%s" "$symbols" | grep -q audout && ! printf "%s" "$symbols" | grep -q audren',
            "verify-sdl2-audout",
            nm,
            archive
        })

        return {
            version = "2.28.5-3",
            includedirs = path.join(root, "include"),
            linkdirs = path.join(root, "lib"),
            links = {"SDL2"},
            syslinks = {"pthread"}
        }
    end)
package_end()
