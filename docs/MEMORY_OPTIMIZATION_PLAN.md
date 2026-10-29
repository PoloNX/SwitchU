# SwitchU memory optimization plan (normal path)

Goal: use less process/GPU memory **without** changing the day-to-day SwitchU
feel (poster browsing, soft-land, Theme Shop, SGDB heroes). Frame Tap light
path (`FT_VIEWER_LIGHT_PATH.md` / #177) stays separate.

Inventory soak uses a **temporary** log gated by a flag file so we can measure,
then turn logging off.

## Measuring (temporary)

| | |
|--|--|
| Enable | Create empty `sdmc:/config/SwitchU/mem_inventory` |
| Disable | Delete that file (rechecked every ~5 s) |
| Interval | One `[mem] tick` line every **15 s** while enabled |
| Extra | `[mem] after-splash-free` when leave-splash GPU is released |

Example line fields: process `free`/`used`/`total`, GPU `img` used/budget,
`headroom`, streamer `bound` / `freeTex` / `empty` / `pending` / `compressedKB`,
folder `peeks` / `composites` / `covers`, font caches, `splash=0|1`, light, layout.

**After the inventory soak:** delete the flag file and remove or `#if 0` the
tick path if the noise is no longer useful (do not leave periodic logging on
in release indefinitely).

## Ranked work

### Done / in progress

1. **Free leave-splash GPU after fade** — ~3.7 MiB full-res tex was retained
   until the next leave overwrite. Now released when fade completes (`[leave]
   splash GPU released`). Disk `leave_frame.bin` kept for the next AppletReturn.
   Confirmed in soak inventory: `splash=0` after handoff.

2. **Half-res leave capture** — `downloadFramebufferRgba(..., halfRes=true)`.
   Splash + disk blob ~4× smaller; stretch-draw for the brief crossfade.

3. **Idle destroy of unbound streamer MemBlocks** — After ~8 s with no page /
   focus change, drip one free-slot GPU retire per frame (skip soft-land /
   splash / morph / FT lite).

4. **Peek/composite/cover caps** — Early warm no longer library-wide bakes;
   drip trim peeks≤32, composites≤24, covers≤24 (far from focus first).

5. **Frosted half-res leave splash** — CPU separable blur at save (full
   brightness); soft stretch reads intentional without a dim wash.

6. **SGDB fit RAM cache** — In-RAM fit cache capped at **4 MiB / 6 entries**
   (was 10 MiB / 16); disk `.rgba-cache` unchanged.

### Near-term (UX-safe)

### Medium (same feel if tuned carefully)

7. **Poster distance LOD** — Focus ±1 full poster; farther slots half-linear
   (¼ pixels) or icon stand-ins, upgrade on focus. Biggest single-row saver.

8. **Font pressure already exists** — Prefer relying on pressure trim
   (1 MiB/font when image avail is low) over cutting the steady 4 MiB ceiling
   globally so labels do not vanish.

9. **Audio embedded covers** — Cap or lazy-load ID3 APIC bytes; QS already
   caps thumbs at 20.

### Do not cut first

- Steady single-row hero / SGDB 1280×720 presentation quality  
- Theme Shop hide hard-release pattern (keep; copy elsewhere)  
- Soft-land timing that makes HOME return feel instant  

## Top holders (reference)

| Holder | Approx / limit |
|--------|----------------|
| GpuDevice image budget | 64 MiB soft |
| IconStreamer single-row | ±4 pages; posters up to ~320×480; free slots keep MemBlocks |
| SGDB fit RAM | ≤4 MiB / 6 entries |
| Fonts (×2) | up to 4 MiB each |
| Folder peeks / composites | capped (32 / 24 / 24) |
| Leave splash | half-res frosted ≈0.9 MiB GPU, released after fade |

## Soak checklist

1. Create `config/SwitchU/mem_inventory`, reboot, browse HOME + folders + Theme Shop.  
2. Collect `[mem]` lines from `config/SwitchU/menu.log` (note `splash=0` after return).  
3. Delete flag file when done.  
4. Use inventory to pick next PR (idle streamer release vs peek LRU vs half-res leave).

## Related

- `docs/FT_VIEWER_LIGHT_PATH.md` — viewer light + NeedHeadroom handshake  
- PR #177 — FT coexistence light path  
