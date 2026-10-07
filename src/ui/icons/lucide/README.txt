Tool + chrome icons vendored from Lucide (https://lucide.dev).
Version: git main 2026-09-24, 174 outline SVGs (118 Lucide + 56 custom-drawn in Lucide style), 24x24 stroke=currentColor width=2.

License: ISC (see LICENSE.lucide). Feather-derived subset additionally MIT
(Cole Bemis) as noted in LICENSE.lucide. Both permissive, commercial-safe,
no attribution required in UI (attribution kept here + About dialog should
mention "Icons: Lucide (ISC)").

Our key -> lucide mapping is by filename (src/ui/icons/lucide/<key>.svg,
embedded as :/icons/lucide/<key>.svg via src/ui/icons.qrc).
Custom-drawn covers niche photo tools with no Lucide equivalent (liquify,
lasso-mag variants, vector persona extras, lq-* warp tools) plus distinct
flyout members (eraser-bg vs eraser, marquee-col vs marquee-row).
Procedural QPainter glyphs remain in icons.cpp as a last-resort fallback.
