project('tomlc99', 'c')
tomlc99_lib = static_library('tomlc99', 'toml.c')
tomlc99_dep = declare_dependency(
  include_directories : include_directories('.'),
  link_with : tomlc99_lib)
