project('sehn', 'cpp',
  version : '0.1.0',
  license : 'MIT',
  default_options : [
    'cpp_std=c++17',
    'warning_level=2',
    'buildtype=release',
  ])

# ── dependencies ────────────────────────────────────────────────────────────────

x11_dep      = dependency('x11')
xext_dep     = dependency('xext')
libjpeg_dep  = dependency('libjpeg')
libpng_dep   = dependency('libpng')
libexif_dep  = dependency('libexif',  required : false)
libcurl_dep  = dependency('libcurl',  required : false)
v4l2_dep     = dependency('libv4l2',  required : true)
toml_dep     = dependency('tomlc99',  required : false,
                 fallback : ['tomlc99', 'tomlc99_dep'])
libav_dep = dependency('libavcodec')
libavf_dep = dependency('libavformat')
libavu_dep = dependency('libavutil')
libavs_dep = dependency('libswscale')
libpulse_dep = dependency('libpulse')

deps = [x11_dep, xext_dep, libjpeg_dep, libpng_dep, v4l2_dep, toml_dep, libav_dep, libavf_dep, libavu_dep, libavs_dep, libpulse_dep]

if libexif_dep.found()
  deps += libexif_dep
  add_project_arguments('-DHAVE_LIBEXIF', language : 'cpp')
endif

if libcurl_dep.found()
  deps += libcurl_dep
  add_project_arguments('-DHAVE_LIBCURL', language : 'cpp')
endif

# ── version/config header ───────────────────────────────────────────────────────

conf = configuration_data()
conf.set_quoted('SEHN_VERSION', meson.project_version())
conf.set_quoted('SEHN_DATADIR', get_option('datadir'))
configure_file(output : 'config.h', configuration : conf)

# ── subdirectories ──────────────────────────────────────────────────────────────

subdir('src')
subdir('man')
subdir('share/applications')
subdir('share/images')

# ── install: docs ───────────────────────────────────────────────────────────────

install_data(
  ['AUTHORS', 'ChangeLog', 'README.md', 'TODO'],
  install_dir : get_option('datadir') / 'doc' / 'sehn')

# ── install: examples ───────────────────────────────────────────────────────────

install_subdir('examples',
  install_dir : get_option('datadir') / 'sehn')

# ── install: fonts ──────────────────────────────────────────────────────────────

install_subdir('share/fonts',
  install_dir : get_option('datadir') / 'fonts' / 'sehn',
  strip_directory : true)

# ── tests ───────────────────────────────────────────────────────────────────────

# if get_option('tests')
  # subdir('test')
# endif
