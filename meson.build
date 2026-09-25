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
subdir('tests')

if get_option('spikes')
  subdir('spikes/s1-full-renderer')
endif
