#!/usr/bin/env bash
set -euo pipefail

readonly SDL2_PKG_NAME="switch-sdl2-2.28.5-3-any.pkg.tar.zst"
readonly SDL2_PKG_URL="https://wii.leseratte10.de/devkitPro/switch/sdl2/${SDL2_PKG_NAME}"
readonly SDL2_PKG_SHA256="b554bde32201f32f93a5be1a6561cf2abb9fd7755e00ebbce8a692b89cf0646e"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

pkg_path="${tmp_dir}/${SDL2_PKG_NAME}"
curl -fsSL "${SDL2_PKG_URL}" -o "${pkg_path}"

actual_sha256="$(sha256sum "${pkg_path}" | awk '{print $1}')"
if [[ "${actual_sha256}" != "${SDL2_PKG_SHA256}" ]]; then
    echo "Unexpected ${SDL2_PKG_NAME} checksum: ${actual_sha256}" >&2
    exit 1
fi

if command -v dkp-pacman >/dev/null 2>&1; then
    dkp-pacman -U --noconfirm "${pkg_path}"
elif command -v pacman >/dev/null 2>&1; then
    pacman -U --noconfirm "${pkg_path}"
else
    echo "Neither dkp-pacman nor pacman was found." >&2
    exit 1
fi

devkitpro="${DEVKITPRO:-/opt/devkitpro}"
sdl_lib="${devkitpro}/portlibs/switch/lib/libSDL2.a"

if [[ ! -f "${sdl_lib}" ]]; then
    echo "SDL2 library not found at ${sdl_lib}" >&2
    exit 1
fi

nm_tool="nm"
if command -v aarch64-none-elf-nm >/dev/null 2>&1; then
    nm_tool="aarch64-none-elf-nm"
fi

symbols="$("${nm_tool}" -g "${sdl_lib}")"
if ! grep -q 'audout' <<<"${symbols}"; then
    echo "Installed SDL2 does not reference audout." >&2
    exit 1
fi
if grep -q 'audren' <<<"${symbols}"; then
    echo "Installed SDL2 still references audren." >&2
    exit 1
fi

echo "Installed ${SDL2_PKG_NAME}; SDL2 audio backend is audout."
