# Frame Tap viewer light path

When a Frame Tap **viewer is connected**, SwitchU temporarily enters a light
memory path so FT can keep ~30 MB applet/process headroom for remount/stream.
With no viewer (or FT not installed), SwitchU stays on the normal full path.

## Detection

Polls FT control service `sftap` `GetStatus` (cmd 0), matching FT's
`StreamStatus` (64 bytes):

- `reserved3 & 4` — network viewer present
- `state == Streaming (4)` — actively streaming (USB/net)

Soak/debug force: create empty file
`sdmc:/config/SwitchU/ft_light_force`.

## While light

1. After reclaim, raise `GpuDevice` alloc refuse headroom from 2 MiB → up to 16 MiB,
   clamped so process free always keeps a 6 MiB UI reserve (never refuse below that
   floor — soak 2026-10-27: free≈8 MiB + fixed 28 MiB headroom starved every image alloc)
2. Cap icon/poster streamer page radius to 2 (focus ±2); cancel shadow prefetch  
3. Drop off-page folder peeks / composites; pause SteamGridDB hero preload  
4. **Release** GPU textures on free streamer slots (eviction alone kept MemBlocks)

## Before resume/launch (viewer connected)

Focus-only radius (0), release unused GPU textures + trim fonts so FT's donor can
remount on the 2nd+ HOME→game cycle. Browse radius restores to 2 on next HOME tick.

## On leave

Restore 2 MiB headroom, clear radius cap, soft-land streamer refill (paced).
With a viewer still connected, soft-land stays at ±2 (no widen to 3).

## Related

- PR #132 — HOME Album gfx soft recovery (separate safety net; not this path)
- Theme Shop already hard-releases preview GPU on hide for FT headroom

## FT ↔ SwitchU NeedHeadroom handshake

Shipped alongside Frame Tap `sftap` cmd **15** `AckHeadroom(u32 free_kb)` and
`StreamStatus.reserved3` bit **6** (`NeedHeadroom`). Before each donor remount,
FT sets the bit (and writes `sdmc:/config/switch-frame-tap/need_headroom`), waits
up to 2 s, then launches the donor.

| Side | Action |
|------|--------|
| FT | `HeadroomAskAndWait(cap_mb, 2000)` before `pmshellLaunchProgram` |
| SwitchU | Poll GetStatus @100 ms; on bit 6 → `reclaimForFrameTapLaunch` |
| SwitchU | `AckHeadroom(free_kb)` + write `sdmc:/config/SwitchU/ft_headroom_ready` |
| FT | On ACK / ready file / timeout → donor launch; `HeadroomClear` after map |

While NeedHeadroom is set, `shot_fails` carries the requested MB (GetDonorCap
still works). File fallback keeps older FT / brief service gaps working.
