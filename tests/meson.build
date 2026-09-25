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

# ── Library tests (design.md §16) ──

adaptive_vectors_h = custom_target('adaptive-vectors-h',
  input: meson.project_source_root() / 'spec' / 'adaptive-vectors.json',
  output: 'adaptive-vectors.h',
  command: [find_program(meson.project_source_root() / 'tools' / 'gen-adaptive-vectors.py'), '@INPUT@', '@OUTPUT@'],
)

# The decision logic alone: plain C, no display.
test('adaptive', executable('test-adaptive',
    'test-adaptive.c',
    meson.project_source_root() / 'lib' / 'adaptive' / 'glass-adaptive.c',
    adaptive_vectors_h,
    include_directories: glass_inc,
    dependencies: [dependency('glib-2.0'), libm_dep],
  ),
  suite: 'lib',
)

test_env = environment()
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_DEBUG', 'fatal-criticals')
test_env.set('GLASS_RENDERER', '')

test('params', executable('test-params',
    'test-params.c', glass_params_table,
    c_args: ['-DGLASS_COMPILATION'],
    dependencies: [glass_dep, adw_dep, libm_dep],
  ),
  env: test_env,
  suite: 'lib',
)

# Needs a display (Wayland or X11); exits 77 = skipped without one.
test('widgets', executable('test-widgets',
    'test-widgets.c', glass_params_table,
    c_args: ['-DGLASS_COMPILATION'],
    dependencies: [glass_dep, adw_dep, libm_dep, epoxy_dep],
  ),
  env: test_env,
  suite: 'lib',
  timeout: 60,
)
