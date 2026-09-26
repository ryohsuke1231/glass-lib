# libglass-lib-1 (design.md §15.1)

version_parts = meson.project_version().split('.')
version_conf = configuration_data()
version_conf.set('GLASS_MAJOR_VERSION', version_parts[0])
version_conf.set('GLASS_MINOR_VERSION', version_parts[1])
version_conf.set('GLASS_MICRO_VERSION', version_parts[2])
version_conf.set('GLASS_VERSION', meson.project_version())
glass_version_h = configure_file(
  input: 'glass-version.h.in',
  output: 'glass-version.h',
  configuration: version_conf,
)

# spec/params.json -> the parameter and material tables (design.md §11).
glass_params_table = custom_target('glass-params-table',
  input: meson.project_source_root() / 'spec' / 'params.json',
  output: 'glass-params-table.h',
  command: [find_program(meson.project_source_root() / 'tools' / 'gen-params.py'), '@INPUT@', '@OUTPUT@'],
)

glass_public_headers = files(
  'glass.h',
  'glass-enums.h',
  'glass-main.h',
  'glass-context.h',
  'glass-view.h',
  'glass-panel.h',
  'glass-toolbar-view.h',
  'glass-header-bar.h',
  'glass-split-view.h',
  'glass-toggle-group.h',
  'glass-button.h',
  'glass-button-group.h',
  'glass-switch.h',
  'glass-slider.h',
  'glass-group.h',
  'glass-popover.h',
  'glass-menu-button.h',
  'glass-dialog.h',
  'glass-tab-bar.h',
  'glass-search-entry.h',
)

glass_public_sources = files(
  'glass-main.c',
  'glass-context.c',
  'glass-view.c',
  'glass-panel.c',
  'glass-toolbar-view.c',
  'glass-header-bar.c',
  'glass-split-view.c',
  'glass-toggle-group.c',
  'glass-button.c',
  'glass-button-group.c',
  'glass-switch.c',
  'glass-slider.c',
  'glass-group.c',
  'glass-popover.c',
  'glass-menu-button.c',
  'glass-dialog.c',
  'glass-tab-bar.c',
  'glass-search-entry.c',
)

glass_private_sources = files(
  'glass-style-node.c',
  'glass-pill-box.c',
  'glass-standalone.c',
  'render/glass-gl.c',
  'render/glass-renderer.c',
  'adaptive/glass-adaptive.c',
)

glass_enums = gnome.mkenums_simple('glass-enum-types',
  sources: 'glass-enums.h',
  header_prefix: '#if !defined(GLASS_INSIDE) && !defined(GLASS_COMPILATION)\n#error "Only <glass.h> can be included directly."\n#endif\n\n#include "glass-enums.h"',
  install_header: true,
  install_dir: get_option('includedir') / 'glass-lib-1',
)

glass_resources = gnome.compile_resources('glass-resources',
  'glass.gresource.xml',
  source_dir: [meson.current_source_dir() / 'style', shader_build_dir],
  dependencies: [glass_core_frag, quad_vert],
  c_name: 'glass',
)

glass_inc = include_directories('.')

libglass = shared_library('glass-lib-1',
  glass_public_sources,
  glass_private_sources,
  glass_enums,
  glass_resources,
  glass_params_table,
  glass_version_h,
  include_directories: glass_inc,
  c_args: ['-DGLASS_COMPILATION', '-DG_LOG_DOMAIN="glass"'],
  dependencies: [gtk_dep, adw_dep, epoxy_dep, libm_dep],
  version: '0.0.0',
  install: true,
)

install_headers(glass_public_headers, glass_version_h, subdir: 'glass-lib-1')

glass_dep = declare_dependency(
  link_with: libglass,
  include_directories: glass_inc,
  sources: [glass_enums[1], glass_version_h],
  dependencies: [gtk_dep],
)

pkgconfig = import('pkgconfig')
pkgconfig.generate(libglass,
  name: 'glass-lib-1',
  description: 'Liquid Glass-style refraction for GTK 4 apps',
  filebase: 'glass-lib-1',
  subdirs: 'glass-lib-1',
  requires: ['gtk4', 'libadwaita-1'],
)

if get_option('introspection')
  glass_gir = gnome.generate_gir(libglass,
    sources: [glass_public_headers, glass_public_sources, glass_enums, glass_version_h],
    namespace: 'Glass',
    nsversion: '1',
    identifier_prefix: 'Glass',
    symbol_prefix: 'glass',
    includes: ['Gtk-4.0', 'Adw-1'],
    header: 'glass.h',
    export_packages: 'glass-lib-1',
    extra_args: ['-DGLASS_COMPILATION', '--quiet', '--warn-all'],
    install: true,
  )
endif
