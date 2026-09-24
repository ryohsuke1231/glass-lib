golden_resources = gnome.compile_resources('golden-resources',
  'golden/golden.gresource.xml',
  source_dir: shader_build_dir,
  dependencies: [glass_core_frag, glass_reference_frag, quad_vert],
  c_name: 'golden',
)

glass_golden = executable('glass-golden',
  'golden/glass-golden.c',
  golden_resources,
  dependencies: [
    epoxy_dep,
    dependency('gio-2.0'),
    dependency('gdk-pixbuf-2.0'),
    libm_dep,
  ],
  install: false,
)

# The shader core against the reference shader (design.md §10.5). Needs a
# headless EGL (Mesa's surfaceless platform); exits 77 = skipped without one.
test('glass-core-golden', glass_golden,
  suite: 'shaders',
  timeout: 300,
)
