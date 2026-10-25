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

### Phase 1 — eShop shelf + product sheet *(this PR / experiment)*

Visible redesign without a full rewrite:

1. **Browse (shelf)**  
   - Cover-dominant tiles (art fills the card; caption strip underneath)  
   - Quieter headers (“Themes”, “Community”, “Icons”)  
   - Soften chrome; keep Active / version chips light  
   - Keep 2×2 or nudge toward a denser eShop shelf if it still reads on TV  

2. **Detail (product sheet)**  
   - Larger hero cover  
   - Title + author + one supporting line (drop dense info columns)  
   - Soft full-bleed scrim; sheet rises cleanly  
   - Primary CTA row stays bottom-right / bottom-forward  

3. **Icons studio**  
   - Same sheet language; tint controls remain a side panel under the hero  

4. **Docs + branch** so we can iterate with PoloNX’s green light

### Phase 2 — Structure

1. Extract `PreviewPipeline` (local + remote adapters) out of `ThemeShopScreen`  
2. Split Themes vs Icons **presenters** under the same overlay chrome  
3. Thin `ThemeShopScreenUi.cpp` (stop `isIconsTab()` branching everywhere)

### Phase 3 — Options + polish

1. Options tab: keep only theme/icon-adjacent prefs (Auto Theme, adaptive contrast)  
2. Move music / grid / sort / cursor prefs back toward Settings  
3. Install progress on-card / thin sheet (avoid nuking Community cache mid-transfer)  
4. Motion pass: 2–3 intentional transitions only

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
