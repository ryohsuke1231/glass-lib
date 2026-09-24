s1_resources = gnome.compile_resources('s1-resources',
  's1.gresource.xml',
  source_dir: shader_build_dir,
  dependencies: [glass_core_frag, glass_reference_frag, quad_vert],
  c_name: 's1',
)

executable('glass-s1',
  's1-main.c',
  's1-glass-view.c',
  's1-gl.c',
  s1_resources,
  dependencies: [gtk_dep, epoxy_dep, libm_dep],
  c_args: ['-DG_LOG_DOMAIN="glass-s1"'],
  install: false,
)
