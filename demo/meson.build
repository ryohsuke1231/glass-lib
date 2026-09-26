# Installs the demos chosen with -Ddemos=gallery,weather (the Flatpaks use
# this, build-aux/flatpak/): the compiled JavaScript, a launcher each, and
# their desktop file, metainfo and icon. For development, run them from the
# source tree instead (README: npm run build, then meson devenv).

demo_js_dir = get_option('datadir') / 'glass-lib' / 'demo' / 'js'

# The TypeScript is compiled when installing: node_modules' tsc in a
# development tree, the SDK's typescript extension in a Flatpak build.
tsc = find_program('tsc', dirs: [meson.current_source_dir() / 'node_modules' / '.bin'])
meson.add_install_script('install-js.sh', tsc.full_path(), meson.current_source_dir(), demo_js_dir)

demo_apps = {
  'gallery': { 'id': 'io.github.ryohsuke1231.GlassGallery', 'command': 'glass-gallery', 'main': 'main.js' },
  'weather': { 'id': 'io.github.ryohsuke1231.GlassWeather', 'command': 'glass-weather', 'main': 'weather/main.js' },
}

foreach name : get_option('demos')
  app = demo_apps[name]
  configure_file(
    input: 'data/launcher.in',
    output: app['command'],
    configuration: { 'JS': get_option('prefix') / demo_js_dir / app['main'] },
    install: true,
    install_dir: get_option('bindir'),
    install_mode: 'rwxr-xr-x',
  )
  install_data('data' / app['id'] + '.desktop', install_dir: get_option('datadir') / 'applications')
  install_data('data' / app['id'] + '.metainfo.xml', install_dir: get_option('datadir') / 'metainfo')
  install_data('data' / 'icons' / app['id'] + '.svg',
    install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')
endforeach
