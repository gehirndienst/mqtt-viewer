project(
  'mqtt-viewer',
  'c',
  version : '1.1.0',
  meson_version : '>=1.10.0',
  default_options : [
    'c_std=gnu23,c23',
    'warning_level=3',
    'werror=true',
    'force_fallback_for=raylib',
  ],
)

cc = meson.get_compiler('c')
host_os = host_machine.system()

# -std=c23 puts glibc in strict-ANSI mode hiding POSIX symbols eg strtok_r, clock_gettime etc
if host_os == 'linux'
  add_project_arguments('-D_GNU_SOURCE', language : 'c')
endif

add_project_arguments('-DAPP_VERSION="@0@"'.format(meson.project_version()), language : 'c')

# check if embedded resources are supported by the compiler (C23 #embed)
if not cc.compiles('static const unsigned char d[] = {\n#embed __FILE__\n};\nint main(void) { return d[0]; }', name : 'C23 #embed support')
    error('C compiler lacks C23 #embed support - use clang >= 19 or GCC >= 15 (e.g. CC=clang meson setup ...)')
endif

# Dependencies
clay_dep = dependency('clay', fallback : ['clay', 'clay_dep'])
raylib_dep = dependency('raylib', fallback : ['raylib', 'raylib_dep'], default_options : ['warning_level=1'])
sqlite3_dep = dependency('sqlite3', fallback : ['sqlite3', 'sqlite3_dep'], default_options : ['werror=false'])

mosquitto_dep = dependency('libmosquitto')
threads_dep = dependency('threads')

m_dep = cc.find_library('m', required : host_os == 'linux')
platform_deps = [m_dep]

app_deps = [raylib_dep, sqlite3_dep, clay_dep, mosquitto_dep, threads_dep] + platform_deps

# Sources
src_model = files(
  'src/model/spsc_queue.c',
  'src/model/pool_alloc.c',
  'src/model/arena_alloc.c',
  'src/model/topic_tree.c',
  'src/model/message_buf.c',
  'src/model/broker_profile.c',
  'src/model/connection_log.c',
  'src/model/app_state.c',
  'src/model/subscription.c',
  'src/model/chart_series.c',
  'src/model/csv_export.c',
  'src/model/util.c',
  'src/model/json_pp.c',
  'src/model/cbor_pp.c',
  'src/model/version.c',
)

src_core = files(
  'src/core/mqtt_client.c',
  'src/core/ssh_tunnel.c',
)

src_ui = files(
  'src/ui/tree_widget.c',
  'src/ui/inspector_widget.c',
  'src/ui/chart_panel.c',
  'src/ui/publish_panel.c',
  'src/ui/profile_dialog.c',
  'src/ui/context_menu.c',
  'src/ui/log_panel.c',
  'src/ui/about_dialog.c',
  'src/ui/menu_bar.c',
  'src/ui/status_bar.c',
  'src/ui/ui_util.c',
  'src/ui/text_edit.c',
)

src_platform = files(
  'src/platform/clay_impl.c',
  'src/platform/db.c',
  'src/platform/ui.c',
  'src/platform/export_path.c',
  'src/platform/update_check.c',
)

src_main = files('src/main.c')

src_inc = include_directories('src', 'src/compat')

app_lib = static_library(
  'mqtt-viewer-lib',
  src_model + src_core + src_ui + src_platform,
  include_directories : src_inc,
  dependencies : app_deps,
  install : false,
)

app_dep = declare_dependency(
  link_with : app_lib,
  include_directories : src_inc,
  dependencies : app_deps,
)

mqtt_viewer = executable(
  meson.project_name(),
  src_main,
  dependencies : app_dep,
  install : true,
)

subdir('tests')
