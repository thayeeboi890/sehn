date_res = run_command('date', '+%B %d, %Y', capture: true, check: false)
date_str = date_res.stdout().strip()

if date_str == ''
  date_str = 'unknown'
endif

custom_target('sehn.1',
  input: 'sehn.pre',
  output: 'sehn.1',
  capture: true,
  command: [
    'sed',
    '-e', 's|[$]DATE[$]|' + date_str + '|g',
    '-e', 's|[$]VERSION[$]|' + meson.project_version() + '|g',
    '-e', 's|[$]MAN_EXIF[$]|' + (libexif_dep.found() ? 'enabled' : 'disabled') + '|g',
    '-e', 's|[$]MAN_CURL[$]|' + (libcurl_dep.found() ? 'enabled' : 'disabled') + '|g',
    '-e', 's|[$]MAN_XINERAMA[$]|disabled|g',
    '-e', 's|[$]MAN_INOTIFY[$]|disabled|g',
    '-e', 's|[$]MAN_V4L2_EXT[$]|disabled|g',
    '-e', '/[$]MAN_DEBUG[$]/d',
    '@INPUT@'
  ],
  install: true,
  install_dir: get_option('mandir') / 'man1'
)
