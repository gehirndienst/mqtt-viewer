project(
  'mqtt-viewer',
  'c',
version : '1.0.3',
  default_options : ['c_std=c23', 'warning_level=3', 'werror=true', 'sqlite3:werror=false']
)

cc = meson.get_compiler('c')

# -std=c23 puts glibc in strict-ANSI mode hiding POSIX symbols eg strtok_r, clock_gettime etc
add_project_arguments('-D_GNU_SOURCE', language : 'c')

# check if embedded resources are supported by the compiler (C23 #embed)
if not cc.compiles('static const unsigned char d[] = {\n#embed __FILE__\n};\nint main(void) { return d[0]; }', name : 'C23 #embed support')
    error('C compiler lacks C23 #embed support - use clang >= 19 or GCC >= 15 (e.g. CC=clang meson setup ...)')
endif

# Dependencies
raylib_dep = dependency('raylib', fallback : ['raylib', 'raylib_dep'])
sqlite3_dep = dependency('sqlite3', fallback : ['sqlite3', 'sqlite3_dep'])
mosquitto_dep = dependency('libmosquitto')
cjson_dep = dependency('libcjson', required : true)
threads_dep = dependency('threads')

# Math library (needed on Linux)
m_dep = cc.find_library('m', required : false)

clay_dep = dependency('clay', fallback : ['clay', 'clay_dep'])

# src
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
    'src/ui/menu_bar.c',
    'src/ui/status_bar.c',
    'src/ui/ui_util.c',
)

src_platform = files(
    'src/platform/clay_impl.c',
    'src/platform/db.c',
    'src/platform/ui.c',
    'src/platform/export_path.c',
)

src_main = files('src/main.c')

src_inc = include_directories('src')

mqtt_viewer = executable('mqtt-viewer',
    src_main + src_model + src_core + src_ui + src_platform,
    include_directories : [src_inc],
    dependencies : [clay_dep, raylib_dep, sqlite3_dep, mosquitto_dep, cjson_dep, m_dep, threads_dep],
    install : true
)

subdir('tests')
