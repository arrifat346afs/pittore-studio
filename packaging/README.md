# Packaging — Pittore Studio (Linux)

Installed by `meson install` (via `src/app/meson.build`):

- `studio.pittore.painter.desktop` → `<datadir>/applications` (Name=Pittore Studio, Exec=`painter`)
- `studio.pittore.painter.metainfo.xml` → `<datadir>/metainfo`
- `mime/painter.xml` → `<datadir>/mime/packages` (`application/x-painter-project`, `*.psc` + legacy `*.ifp`)
- `icons/hicolor/scalable/apps/painter.svg` → `<datadir>/icons/hicolor/scalable/apps`

The app icon (`painter.svg`) is a user-supplied original asset for Pittore
Studio (it carries a C2PA manifest describing its provenance).
