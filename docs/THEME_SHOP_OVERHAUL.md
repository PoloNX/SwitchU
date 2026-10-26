# Theme Shop overhaul

Experiment branch: `feat/themeshop-eshop-overhaul`  
Baseline: Atmos / #171 checkpoint (`checkpoint/enjoyment-atmos-20261025-1005`)

## North star

Make SwitchU’s Theme Shop feel like the best of **Nintendo eShop** and **Nintendo Switch Online** over the years:

- **Cover first.** Art is the product. Chrome stays quiet.
- **One clear job per screen.** Browse → open → Apply / Get. No dashboard clutter.
- **Soft motion.** Page slides, detail rise, gentle focus — presence, not noise.
- **Short copy.** Name + author. Metadata whispers; it never competes with the cover.
- **One primary CTA.** Apply / Get / Install reads as the hero action.
- **Warm, spacious, legible.** Soft scrims, frosted sheets, room to breathe.

We are **not** throwing away catalog / HTTP / install / draft-tint. Those already work. We redesign presentation and carve the god-object into clearer pieces.

## What stays

- Draft vs Apply for icon tint (correct mental model)
- Disk-first catalog + GPU preview budgets
- App callbacks for apply / delete / download
- Atmos + Flatten / Grayscale / Custom tint
- Sleep / wake preview cancel + empty-page Minus guard

## Phases

### Phase 1 — eShop shelf + product sheet *(done on this branch)*

Visible redesign without a full rewrite:

1. **Browse (shelf)** — done  
   - Cover-dominant 3×2 tiles; short headers; shared `ThemeShopLayout.hpp`  
2. **Detail (product sheet)** — done (hero + quiet copy + stronger primary CTA)  
3. **Icons studio** — Flatten keeps mean hue; Flatten+Gray honors Light  
4. **Community covers** — prefetch aligned to 3-col shelf; cache reserve when leaving tab  
5. **First-focus hitch** — warm disk catalog + first shelf covers while still on Installed  

Checkpoint before hitch / Phase 2: `checkpoint/themeshop-eshop-20261025-1538`

### Phase 2 — Structure

1. Extract `PreviewPipeline` (local + remote adapters) out of `ThemeShopScreen` — *disk cache skeleton landed*  
2. Split Themes vs Icons **presenters** under the same overlay chrome  
3. Thin `ThemeShopScreenUi.cpp` (stop `isIconsTab()` branching everywhere)

### Phase 3 — Options + polish

1. Options tab: SwitchU personalization stays here (music, grid, hints, cursor/page, Auto Theme, adaptive contrast, theme-music Apply policy) — restored after brief Settings move  
2. Settings → Audio / Display stays system-adjacent (brightness, TV, speaker mute, etc.)  
3. Install progress on-card / thin sheet (avoid nuking Community cache mid-transfer) — *done*  
4. Motion pass: page slide + detail rise — *done*  
5. Confirms: Remove + Download/Apply (Install stays one-tap) — *done*

## Non-goals (for now)

- Rewriting `ThemeCatalogClient` / GitHub installer  
- New remote APIs or store accounts  
- Replacing TabbedOverlayScreen chrome wholesale  
- Shipping a separate “Theme Shop 2” app

## Success criteria

- Opening Theme Shop feels like walking into a store shelf, not a settings page  
- Detail feels like an eShop product page: big art, short text, clear Get/Apply  
- Icons tint studio still works (draft / Apply / sleep-safe)  
- No regress on Community refresh, install, or HOME apply  
- HW feel on TV: readable at 10ft, soft focus, no spinner flash on scrub

## Risks

- Touch / focus hit-rects must track new layouts  
- Preview decode sizes may need a bump if tiles or hero grow  
- #171 may still be open upstream — this branch is intentionally experimental

## Working agreement

Iterate on device (FTP). Prefer small visual commits over a months-long rewrite. If a structural extract blocks the feel, ship the feel first.
