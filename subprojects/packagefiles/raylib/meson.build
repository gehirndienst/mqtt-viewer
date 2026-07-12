project('raylib', 'c', version: '5.5')
cc = meson.get_compiler('c')
raylib_cargs = [
    '-DPLATFORM_DESKTOP', '-DGRAPHICS_API_OPENGL_33',
    '-Wno-unused-parameter', '-Wno-unused-but-set-variable', '-Wno-error',
]
raylib_inc = include_directories('src', 'src/external/glfw/include')
raylib_deps = []
if host_machine.system() == 'darwin'
    raylib_deps += dependency('appleframeworks', modules: [
        'OpenGL', 'Cocoa', 'IOKit', 'CoreFoundation',
        'CoreGraphics', 'CoreServices', 'Foundation', 'AppKit',
    ])
elif host_machine.system() == 'linux'
    raylib_deps += [
        dependency('gl'),
        dependency('x11', required: false),
        dependency('xrandr', required: false),
        dependency('xi', required: false),
        dependency('xcursor', required: false),
        dependency('xinerama', required: false),
        cc.find_library('m'),
        cc.find_library('dl'),
        cc.find_library('pthread'),
    ]
endif

# rglfw.c is Objective-C on macOS (Cocoa); on Linux GLFW needs an explicit
# platform define or it fails with "Cannot disable Wayland and X11 at the same time"
rglfw_cargs = raylib_cargs
if host_machine.system() == 'darwin'
    rglfw_cargs += ['-x', 'objective-c']
elif host_machine.system() == 'linux'
    rglfw_cargs += ['-D_GLFW_X11']
endif
rglfw_lib = static_library('rglfw', 'src/rglfw.c',
    c_args: rglfw_cargs,
    include_directories: raylib_inc,
    dependencies: raylib_deps)
raylib_src = files(
    'src/rcore.c', 'src/rmodels.c', 'src/rshapes.c',
    'src/rtext.c', 'src/rtextures.c', 'src/raudio.c', 'src/utils.c')
raylib_lib = static_library('raylib', raylib_src,
    c_args: raylib_cargs,
    include_directories: raylib_inc,
    dependencies: raylib_deps,
    link_with: rglfw_lib,
    install: false)
raylib_dep = declare_dependency(
    link_with: raylib_lib,
    include_directories: raylib_inc,
    dependencies: raylib_deps)
