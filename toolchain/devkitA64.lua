toolchain("devkita64")
    set_kind("standalone")
    on_check("check")

    on_load(function(toolchain)
        local DEVKITPRO = os.getenv("DEVKITPRO") or "/opt/devkitpro"
        local DKA64    = path.join(DEVKITPRO, "devkitA64")
        local LIBNX    = path.join(DEVKITPRO, "libnx")
        local bin      = path.join(DKA64, "bin")
        local pre      = "aarch64-none-elf-"

        -- ── Compiler tools ───────────────────────────────
        toolchain:set("toolset", "cc",     path.join(bin, pre .. "gcc"))
        toolchain:set("toolset", "cxx",    path.join(bin, pre .. "g++"))
        toolchain:set("toolset", "ld",     path.join(bin, pre .. "g++"))
        toolchain:set("toolset", "sh",     path.join(bin, pre .. "g++"))
        toolchain:set("toolset", "ar",     path.join(bin, pre .. "gcc-ar"))
        toolchain:set("toolset", "ranlib", path.join(bin, pre .. "gcc-ranlib"))
        toolchain:set("toolset", "strip",  path.join(bin, pre .. "strip"))
        toolchain:set("toolset", "as",     path.join(bin, pre .. "gcc"))

        -- ── Architecture flags ───────────────────────────
        toolchain:add("cxflags",
            "-march=armv8-a+crc+crypto", "-mtune=cortex-a57",
            "-mtp=soft", "-fPIE",
            "-ffunction-sections", "-fdata-sections",
            {force = true})

        toolchain:add("asflags",
            "-g",
            "-march=armv8-a+crc+crypto", "-mtune=cortex-a57",
            "-mtp=soft", "-fPIE",
            {force = true})

        toolchain:add("ldflags",
            "-specs=" .. path.join(LIBNX, "switch.specs"),
            "-march=armv8-a+crc+crypto", "-mtune=cortex-a57",
            "-mtp=soft", "-fPIE",
            "-Wl,--gc-sections",
            {force = true})

        -- ── Defines ──────────────────────────────────────
        toolchain:add("defines", "__SWITCH__")

        -- ── libnx (always needed for switch.specs / <switch.h>) ──
        toolchain:add("includedirs", path.join(LIBNX, "include"))
        toolchain:add("linkdirs", path.join(LIBNX, "lib"))
		
		local PORTLIBS = path.join(DEVKITPRO, "portlibs", "switch")
        toolchain:add("includedirs", path.join(PORTLIBS, "include"))
        toolchain:add("linkdirs", path.join(PORTLIBS, "lib"))
    end)
toolchain_end()
