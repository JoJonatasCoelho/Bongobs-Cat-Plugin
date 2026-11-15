cmake_minimum_required(VERSION 3.16)

project(bongobs-cat VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
set(APP_NAME bongobs-cat)
set(SDK_ROOT_PATH  ${CMAKE_CURRENT_SOURCE_DIR})
set(CORE_PATH      ${SDK_ROOT_PATH}/CubismSdk/Core)
set(FRAMEWORK_PATH ${SDK_ROOT_PATH}/CubismSdk/Framework)
set(THIRD_PARTY_PATH ${SDK_ROOT_PATH}/thirdParty)
set(STB_PATH       ${THIRD_PARTY_PATH}/stb)
set(GLEW_PATH      ${THIRD_PARTY_PATH}/glew)
set(GLFW_PATH      ${THIRD_PARTY_PATH}/glfw)

# Default user-local install prefix on Linux:
#   ~/.config/obs-studio/plugins/bongobs-cat/{bin,data}
if(NOT WIN32 AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.config/obs-studio"
      CACHE PATH "Install prefix" FORCE)
endif()

# ---------------------------------------------------------------------------
# libobs (OBS Studio development package)
# ---------------------------------------------------------------------------
find_package(libobs QUIET)
if(NOT libobs_FOUND)
  message(FATAL_ERROR
    "[${APP_NAME}] libobs não encontrado / libobs not found.\n"
    "Instale o pacote de desenvolvimento do OBS Studio (ex.: 'libobs-dev',\n"
    "'obs-studio-devel' ou use o obs-plugintemplate) e/ou informe o prefixo:\n"
    "  cmake -S . -B build -DCMAKE_PREFIX_PATH=/caminho/para/libobs\n"
    "  cmake -S . -B build -Dlibobs_DIR=/caminho/para/lib/cmake/libobs")
endif()

# ---------------------------------------------------------------------------
# Cubism Core
# ---------------------------------------------------------------------------
if(WIN32)
  # Detect architecture.
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH x86_64)
  else()
    set(ARCH x86)
  endif()

  # Detect compiler.
  if(MSVC_VERSION MATCHES "1800")
    # Visual Studio 2013
    set(COMPILER 120)
  elseif(MSVC_VERSION MATCHES "1900")
    # Visual Studio 2015
    set(COMPILER 140)
  elseif(MSVC_VERSION GREATER_EQUAL 1910 AND MSVC_VERSION LESS 1920)
    # Visual Studio 2017
    set(COMPILER 141)
  elseif(MSVC_VERSION GREATER_EQUAL 1920 AND MSVC_VERSION LESS 1930)
    # Visual Studio 2019
    set(COMPILER 142)
  elseif(MSVC)
    message(FATAL_ERROR "[${APP_NAME}] Unsupported Visual C++ compiler used.")
  else()
    message(FATAL_ERROR "[${APP_NAME}] Unsupported compiler used.")
  endif()

  # Detect core CRT.
  if(CORE_CRL_MD)
    set(CRT MD)
  else()
    set(CRT MT)
  endif()

  set(CORE_LIB_SUFFIX ${CORE_PATH}/lib/windows/${ARCH}/${COMPILER})

  add_library(Live2DCubismCore STATIC IMPORTED)
  set_target_properties(Live2DCubismCore PROPERTIES
    IMPORTED_LOCATION_DEBUG
      ${CORE_LIB_SUFFIX}/Live2DCubismCore_${CRT}d.lib
    IMPORTED_LOCATION_RELWITHDEBINFO
      ${CORE_LIB_SUFFIX}/Live2DCubismCore_${CRT}d.lib
    IMPORTED_LOCATION_RELEASE
      ${CORE_LIB_SUFFIX}/Live2DCubismCore_${CRT}.lib
    IMPORTED_LOCATION_MINSIZEREL
      ${CORE_LIB_SUFFIX}/Live2DCubismCore_${CRT}.lib
    INTERFACE_INCLUDE_DIRECTORIES ${CORE_PATH}/include
  )
else()
  # Linux: prebuilt static core shipped with the Cubism SDK.
  set(CORE_LIB ${CORE_PATH}/lib/linux/x86_64/libLive2DCubismCore.a)
  if(NOT EXISTS "${CORE_LIB}")
    message(FATAL_ERROR
      "[${APP_NAME}] Cubism Core não encontrado / not found: ${CORE_LIB}")
  endif()

  add_library(Live2DCubismCore STATIC IMPORTED)
  set_target_properties(Live2DCubismCore PROPERTIES
    IMPORTED_LOCATION ${CORE_LIB}
    INTERFACE_INCLUDE_DIRECTORIES ${CORE_PATH}/include
  )
endif()

# ---------------------------------------------------------------------------
# GLEW (compiled directly from source to avoid its CMake 2.8 requirement)
# ---------------------------------------------------------------------------
add_library(glew_s STATIC ${GLEW_PATH}/src/glew.c)
target_include_directories(glew_s PUBLIC ${GLEW_PATH}/include)
target_compile_definitions(glew_s PUBLIC GLEW_STATIC GLEW_NO_GLU)
if(NOT WIN32)
  # On Linux GLEW must resolve the GL entry points through EGL
  # (eglGetProcAddress / eglewInit) instead of GLX, so that it works without
  # an X11 connection (pure Wayland / Flatpak sandbox).
  target_compile_definitions(glew_s PUBLIC GLEW_EGL)
endif()
set_target_properties(glew_s PROPERTIES FOLDER "plugins/${APP_NAME}")

# ---------------------------------------------------------------------------
# GLFW (Windows only; Linux renders into an offscreen EGL pbuffer)
# ---------------------------------------------------------------------------
if(WIN32)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
  set(GLFW_USE_WAYLAND    OFF CACHE BOOL "" FORCE)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  add_subdirectory(${GLFW_PATH} ${CMAKE_BINARY_DIR}/thirdParty/glfw)
  set_target_properties(glfw PROPERTIES FOLDER "plugins/${APP_NAME}")
endif()

# ---------------------------------------------------------------------------
# Cubism Framework (OpenGL renderer)
# ---------------------------------------------------------------------------
set(FRAMEWORK_SOURCE OpenGL)
add_subdirectory(${FRAMEWORK_PATH} ${CMAKE_BINARY_DIR}/CubismSdk/Framework)

if(WIN32)
  target_compile_definitions(Framework PUBLIC CSM_TARGET_WIN_GL)
else()
  target_compile_definitions(Framework PUBLIC CSM_TARGET_LINUX_GL)
endif()
target_include_directories(Framework PUBLIC ${GLEW_PATH}/include)
target_link_libraries(Framework PUBLIC Live2DCubismCore glew_s)
set_target_properties(Framework PROPERTIES FOLDER "plugins/${APP_NAME}")

# ---------------------------------------------------------------------------
# System libraries
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
find_package(X11 QUIET)

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
set(${APP_NAME}_SOURCES
  vtuber_plugin.cpp
  VtuberPlugin.cpp
  VtuberFrameWork.cpp
  VtuberDelegate.cpp
  View.cpp
  Live2DManager.cpp
  Model.cpp
  Pal.cpp
  LAppTextureManager.cpp
  LAppAllocator.cpp
  Sprite.cpp
  HookWin32.cpp
  HookLinux.cpp
  EventManager.cpp
  Define.cpp
  jsoncpp.cpp
  InfoReader.cpp
  EglContext.cpp
)

add_library(${APP_NAME} MODULE ${${APP_NAME}_SOURCES})

set_target_properties(${APP_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  POSITION_INDEPENDENT_CODE ON
  FOLDER "plugins/${APP_NAME}"
)

if(NOT WIN32)
  # Produce "bongobs-cat.so" (not "libbongobs-cat.so").
  set_target_properties(${APP_NAME} PROPERTIES PREFIX "")
endif()

target_include_directories(${APP_NAME} PRIVATE ${STB_PATH})

target_link_libraries(${APP_NAME} PRIVATE
  OBS::libobs
  Framework
  glew_s
  OpenGL::GL
  Threads::Threads
  ${CMAKE_DL_LIBS}
)

if(WIN32)
  target_link_libraries(${APP_NAME} PRIVATE glfw)
else()
  # The plugin no longer needs GLFW on Linux: the GL context is a surfaceless
  # EGL one.  Link libEGL directly (GLEW_EGL resolves GL through it) and keep
  # the X11 dependency only for the input hook.
  target_link_libraries(${APP_NAME} PRIVATE OpenGL::EGL)
endif()

if(X11_FOUND)
  target_link_libraries(${APP_NAME} PRIVATE X11::X11)
endif()

if(NOT WIN32)
  # Cubism Core references sincosf/powf/atan2f -> libm.
  target_link_libraries(${APP_NAME} PRIVATE m)
  # Do not export symbols pulled in from static archives (Framework, GLEW,
  # Cubism Core). Only the plugin entry points stay visible.
  target_link_options(${APP_NAME} PRIVATE "-Wl,--exclude-libs,ALL")
endif()

# The prebuilt Linux Cubism Core archive contains PlatformLogger_Linux.c.o,
# which is not position-independent (it references the preemptible libc
# `stdout`), so the linker refuses to put it into a shared module. Provide a
# PIC replacement for its only exported entry point; the non-PIC archive
# member is then never pulled in by the linker.
if(NOT WIN32)
  set(CUBISM_CORE_LOG_SHIM ${CMAKE_BINARY_DIR}/cubism_core_linux_shim.c)
  file(WRITE ${CUBISM_CORE_LOG_SHIM}
    "/* Generated by CMake: PIC shim for the prebuilt Cubism Core. */\n"
    "#include <stdio.h>\n"
    "void csmiPlatformDependentLogPrint(const char *message)\n"
    "{\n"
    "    fprintf(stdout, \"%s\\n\", message);\n"
    "}\n")
  set_source_files_properties(${CUBISM_CORE_LOG_SHIM}
    PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden")
  target_sources(${APP_NAME} PRIVATE ${CUBISM_CORE_LOG_SHIM})
endif()

# ---------------------------------------------------------------------------
# Install (user-local OBS plugin layout)
# ---------------------------------------------------------------------------
install(TARGETS ${APP_NAME}
  LIBRARY DESTINATION plugins/bongobs-cat/bin/64bit
  RUNTIME DESTINATION plugins/bongobs-cat/bin/64bit
)

install(DIRECTORY data/locale
  DESTINATION plugins/bongobs-cat/data)

install(DIRECTORY "Resources/Bango Cat"
  DESTINATION plugins/bongobs-cat/data)
