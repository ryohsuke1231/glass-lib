project('glass-lib', 'c',
  version: '0.0.1',
  license: 'MIT',
  meson_version: '>= 1.0',
  default_options: [
    'c_std=gnu11',
    'warning_level=2',
  ],
)

gnome = import('gnome')

gtk_dep = dependency('gtk4', version: '>= 4.22')
adw_dep = dependency('libadwaita-1', version: '>= 1.9')
epoxy_dep = dependency('epoxy')
libm_dep = meson.get_compiler('c').find_library('m', required: false)

add_project_arguments(
  '-Wno-unused-parameter',
  '-Wno-missing-field-initializers',
  language: 'c',
)

subdir('shaders')
subdir('lib')

if get_option('tests')
  subdir('tests')
endif

if get_option('examples')
  subdir('examples')
endif

if get_option('documentation')
  subdir('docs/reference')
endif

if get_option('demos').length() > 0
  subdir('demo')
endif

# The spikes are not in the published repository: build them where they are.
if get_option('spikes') and import('fs').exists('spikes/s1-full-renderer/meson.build')
  subdir('spikes/s1-full-renderer')
endif
