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

1. Raise `GpuDevice` alloc refuse headroom from 2 MiB → 28 MiB  
2. Cap icon/poster streamer page radius to 1; cancel shadow prefetch  
3. Drop off-page folder peeks / composites; pause SteamGridDB hero preload  

## On leave

Restore 2 MiB headroom, clear radius cap, soft-land streamer refill (paced).

## Related

- PR #132 — HOME Album gfx soft recovery (separate safety net; not this path)
- Theme Shop already hard-releases preview GPU on hide for FT headroom
