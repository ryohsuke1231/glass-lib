# Shaders: expand the #include lines at build time and validate the result.
#
#   glass-core.frag       the library's glass pass (core + supersampling)
#   glass-reference.frag  the unmodified reference shader, wrapped (tests, S1)
#   quad.vert             the shared full-target quad

glsl_include = find_program(meson.project_source_root() / 'tools' / 'glsl-include.py')

glass_core_frag = custom_target('glass-core-frag',
  input: 'targets/gl/glass.frag',
  output: 'glass-core.frag',
  command: [glsl_include, '@INPUT@', '@OUTPUT@', '--depfile', '@DEPFILE@'],
  depfile: 'glass-core.frag.d',
)

glass_reference_frag = custom_target('glass-reference-frag',
  input: 'targets/compat/reference.frag',
  output: 'glass-reference.frag',
  command: [glsl_include, '@INPUT@', '@OUTPUT@', '--depfile', '@DEPFILE@'],
  depfile: 'glass-reference.frag.d',
)

quad_vert = configure_file(
  input: 'targets/gl/quad.vert',
  output: 'quad.vert',
  copy: true,
)

shader_build_dir = meson.current_build_dir()

# glslangValidator checks every shader as GLSL ES 3.00 and GLSL 3.30 core
# (CLAUDE.md: shader changes must pass this).
glslang = find_program('glslangValidator', required: false)
if glslang.found()
  headers = {
    'es': ['--line', '#version 300 es', '--line', 'precision highp float;', '--line', 'precision highp int;'],
    'gl': ['--line', '#version 330 core'],
  }
  sources = {
    'glass-core': ['targets/gl/glass.frag', 'frag'],
    'glass-reference': ['targets/compat/reference.frag', 'frag'],
    'quad': ['targets/gl/quad.vert', 'vert'],
  }
  foreach name, src : sources
    foreach flavour, header : headers
      checked = custom_target('glsl-check-@0@-@1@'.format(name, flavour),
        input: src[0],
        output: '@0@-@1@.@2@'.format(name, flavour, src[1]),
        command: [glsl_include, '@INPUT@', '@OUTPUT@', header,
                  '--depfile', '@DEPFILE@'],
        depfile: '@0@-@1@.@2@.d'.format(name, flavour, src[1]),
        build_by_default: true,
      )
      test('glsl-@0@-@1@'.format(name, flavour), glslang,
        args: ['-S', src[1], checked],
        suite: 'shaders',
      )
    endforeach
  endforeach
endif
