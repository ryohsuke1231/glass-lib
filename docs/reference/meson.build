# The API reference (gi-docgen): -Ddocumentation=true. The output goes to
# build/docs/reference/glass-lib-1/ (index.html) and is installed under
# share/doc/glass-lib-1.

assert(get_option('introspection'), 'the documentation is made from the GIR: -Dintrospection=true')
gidocgen = find_program('gi-docgen', required: true)

glass_toml = configure_file(
  input: 'glass.toml.in',
  output: 'glass.toml',
  configuration: { 'version': meson.project_version() },
)

# The page of parameters and materials, from spec/params.json.
parameters_md = custom_target('parameters-md',
  input: meson.project_source_root() / 'spec' / 'params.json',
  output: 'parameters.md',
  command: [find_program(meson.project_source_root() / 'tools' / 'gen-params-doc.py'), '@INPUT@', '@OUTPUT@'],
)

content_files = files(
  'getting-started.md',
  'concepts.md',
  'tuning.md',
  'styling.md',
  'debugging.md',
  'urlmap.js',
)

custom_target('glass-doc',
  input: [glass_toml, glass_gir[0]],
  output: 'glass-lib-1',
  command: [
    gidocgen, 'generate',
    '--quiet',
    '--fatal-warnings',
    '--add-include-path=@0@'.format(meson.project_build_root() / 'lib'),
    '--config=@INPUT0@',
    '--output-dir=@OUTPUT@',
    '--no-namespace-dir',
    '--content-dir=@0@'.format(meson.current_source_dir()),
    '--content-dir=@0@'.format(meson.current_build_dir()),
    '@INPUT1@',
  ],
  depend_files: content_files,
  depends: [parameters_md],
  build_by_default: true,
  install: true,
  install_dir: get_option('datadir') / 'doc',
)
