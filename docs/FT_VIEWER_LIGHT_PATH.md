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
2. Cap icon/poster streamer page radius to **3** (focus ±3); cancel shadow prefetch  
3. **Suppress folder covers** (peeks/composites/covers) in grid and single-row —
   no warm/bake while light; re-arm fill on leave (empty folders show centered name)  
4. **Suppress SGDB heroes** (release + disable on enter; all `showFocused*` paths
   no-op while light) — ~8 MiB traded for the wider ±3 poster radius  
5. **Release** GPU textures on free streamer slots (eviction alone kept MemBlocks)

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
up to 2 s, then (today) may launch the donor.

| Side | Action |
|------|--------|
| FT | While `AppletBusy` and 100D is up (`home_menu`), ask NeedHeadroom (~30 s cadence); **clear bit** if Applet pool still tight (anti-thrash) |
| FT | Again `HeadroomAskAndWait(cap_mb, 2000)` inside `DonorObtain` before `pmshellLaunchProgram` |
| SwitchU | Poll GetStatus @100 ms; on bit 6 → hard reclaim once, then `AckHeadroom` |
| SwitchU | Boot heap always starts ~304 MB (sm/fs not up in `__libnx_initheap`, so FT cannot be probed yet). After `sdmc` mount: **grow** to the full ladder if FT `config.ini` is absent; **keep lean** if FT is installed. Runtime shrink does not free Applet-pool pages (soak: process total unchanged) |
| FT | `detach_on_home=1` ends capture on HOME (“game is not on screen”); set `0` to keep a paused-game stream. `home_menu=1` is separate (beta HOME capture) |
| SwitchU | `AckHeadroom(free_kb)` + write `sdmc:/config/SwitchU/ft_headroom_ready` |
| SwitchU | **30 s hold:** no browse restore / no re-reclaim (ACK-only); stops icon flash |
| FT | On ACK / ready file → proceed when pool has room; **`HeadroomClear` after map** |
| FT | HBL 100D counts as Album for Beside margins (not “random heavy applet”) |

While NeedHeadroom is set, requested MB is via **`GetDonorCap` (cmd 13)** (not
`shot_fails` — overlay/app show that as screenshot failures). File fallback
`need_headroom` keeps older FT / brief service gaps working.

NeedHeadroom reclaim (SwitchU): drop **all** streamer GPU (incl. focus), SGDB
heroes, peeks/covers, fonts, leave-splash — soft keep-one left pool free stuck
at ~10 MB (`ACK free=10376` forever, soak 2026-10-28). Hard reclaim can reach
~59 MB free (2nd+ HOME after a title); UI must not stay starved under hold.

### FT boot / first remount (landed in applet-mitm tree)

See Frame Tap `tier4/applet-mitm/HANDOFF-SWITCHU-BOOT-CAPTURE.md`:

1. **No** donor launch on NeedHeadroom timeout while SwitchU holds the Applet pool  
2. Wait / re-ask up to ~15 s until Applet-pool room, else defer (engines retry)  
3. Anti-thrash: clear NeedHeadroom while spinning in `vb:applet_busy`

### Boot “Connected — no game” with SwitchU (soak 2026-10-31)

Lean SwitchU heap fixes donor/NeedHeadroom at boot (Applet free enough; ACK
`free≈190 MB`). Remaining gap is **FT capture path**, not SwitchU memory:

1. FT arms `home_menu`, wraps Album `dispdrv`, sets up engines, donor maps OK  
2. `live-ui: vi's shared buffer not found - the HOME menu is not streamed`  
   (stock qlaunch buffer; SwitchU replaced HOME, so this never appears)  
3. Viewer falls to `live:no_game` until a title runs  
4. After game → HOME, 100D reopens `vi:m` **after** FT’s mitm is up →  
   `Album / hbmenu is in front` attach works (first HOME capture of SwitchU)

**FT-side fix:** when `home_menu` and live-ui shared buffer is missing, attach to
the already-running 100D / Album the same way the post-HOME path does — do not
wait for a game (seed + content-sync). Mitm registers ~32 s after boot, after
SwitchU’s first window was already open — reconnecting `vi` *early* does not help.

**SwitchU follow-up (capture mitm remount):** if FT is **absent** when the menu
first probes (cold boot — window opened before mitm), one-shot
`GpuDevice::remountDisplayForCaptureMitm()` when a viewer connects. **Not** on
HOME return: a restarted 100D already opens through the mitm; remounting again
is a black flash. Seed path covers any seconds before the boot remount.

Normal-path mem: `MEMORY_OPTIMIZATION_PLAN.md`.
