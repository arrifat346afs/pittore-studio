# src/app/meson.build
# The application entry point. Installed system-wide (prefix /usr) so
# 'sudo just install' / build.sh --install put it on PATH.

if has_qt6
  executable('painter',
    files('main.cpp'),
    dependencies : [pittore_ui_dep, pittore_compute_dep, qt6dep],
    include_directories : src_inc,
    install : true)
  # MCP stdio shim for AI clients driving the live session (no GUI here;
  # it forwards over the app's local-socket bridge). Installed so clients
  # can find it on PATH.
  executable('pittore-mcp',
    files('mcp_main.cpp'),
    dependencies : [pittore_ui_dep, pittore_compute_dep, qt6dep],
    include_directories : src_inc,
    install : true)
  install_data('../../packaging/linux/studio.pittore.painter.desktop',
    install_dir : get_option('datadir') / 'applications')
  install_data('../../packaging/linux/studio.pittore.painter.metainfo.xml',
    install_dir : get_option('datadir') / 'metainfo')
  install_data('../../packaging/linux/mime/painter.xml',
    install_dir : get_option('datadir') / 'mime/packages')
  install_data('../../packaging/linux/icons/hicolor/scalable/apps/painter.svg',
    install_dir : get_option('datadir') / 'icons/hicolor/scalable/apps')
else
  warning('Qt6 not found — building engine + tests only (pass -Dapp=enabled to force)')
endif