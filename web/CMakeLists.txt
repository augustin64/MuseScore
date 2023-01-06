cmake_minimum_required(VERSION 3.14)

project(webmscore LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MU_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(PROJECT_ROOT_DIR ${MU_ROOT})

set(CMAKE_MODULE_PATH
    ${MU_ROOT}/build
    ${MU_ROOT}/build/cmake
    ${CMAKE_MODULE_PATH}
    )

set(QT_SUPPORT ON)

# include(SetupBuildEnvironment)
include(GetPlatformInfo)

include_directories(
    ${CMAKE_CURRENT_LIST_DIR}
    ${MU_ROOT}/thirdparty/dtl
)


add_definitions(
    # -DNO_QT_SUPPORT
    -DENGRAVING_NO_QTEXTDOCUMENT
    -DENGRAVING_NO_INTERACTIVE
    -DDRAW_NO_QSVGRENDER           # QSvgRenderer (QtSvg) depends on QWidget in Qt5, this changes in Qt6
    # -DHAW_LOGGER_QT_SUPPORT
    -DQT_SUPPORT
    # -DHAW_PROFILER_ENABLED
    -DNDEBUG
    -DQT_NO_DEBUG
)

set(CMAKE_EXECUTABLE_SUFFIX ".lib.js")

set(WASM_LINK_FLAGS         "${WASM_LINK_FLAGS} --bind")
set(WASM_LINK_FLAGS         "${WASM_LINK_FLAGS} --source-map-base ./")
set(WASM_LINK_FLAGS         "${WASM_LINK_FLAGS} --no-entry")
set(WASM_LINK_FLAGS         "${WASM_LINK_FLAGS} --emit-symbol-map")
# set(WASM_LINK_FLAGS         "${WASM_LINK_FLAGS} -s WASM=0 -s SINGLE_FILE=1")
set(WASM_LINK_FLAGS         "${WASM_LINK_FLAGS} -s WASM=1")
set(WASM_LINK_FLAGS         "${WASM_LINK_FLAGS} -s EXPORT_ES6=1 -s USE_ES6_IMPORT_META=0 -s MODULARIZE=1")
set(WASM_LINK_FLAGS         "${WASM_LINK_FLAGS} -s NODEJS_CATCH_EXIT=0 -s NODEJS_CATCH_REJECTION=0")
set(WASM_LINK_FLAGS         "${WASM_LINK_FLAGS} -s ALLOW_TABLE_GROWTH=1")
set(WASM_LINK_FLAGS         "${WASM_LINK_FLAGS} -s ALLOW_MEMORY_GROWTH=1")
# set(WASM_LINK_FLAGS         "${WASM_LINK_FLAGS} -s ASSERTIONS=1")
set(WASM_LINK_FLAGS         "${WASM_LINK_FLAGS} -s EXTRA_EXPORTED_RUNTIME_METHODS='[\"_malloc\", \"_free\", \"ccall\", \"cwrap\", \"stringToUTF8\", \"UTF8ToString\", \"getValue\", \"setValue\"]'")

set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} -std=gnu++11 -fsigned-char")
set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} -fPIC")
set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} -Wno-inconsistent-missing-override")
set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} -Wno-deprecated")
# set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} -Wno-deprecated-copy")
set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")
set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} -s USE_ZLIB=1")      # 1 = use zlib from emscripten-ports
# set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} -s USE_FREETYPE=1")  # 1 = use freetype from emscripten-ports
set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} -s USE_VORBIS=1")    # 1 = use vorbis from emscripten-ports
set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} -s USE_OGG=1")       # 1 = use ogg from emscripten-ports
set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} -s DEMANGLE_SUPPORT=1")

set(CMAKE_CXX_FLAGS_DEBUG   "-g4 -s ASSERTIONS=2 -s STACK_OVERFLOW_CHECK=2 -s SAFE_HEAP=1")
set(CMAKE_CXX_FLAGS_RELEASE "-Oz -DNDEBUG -DQT_NO_DEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} -Wall -Wextra -Woverloaded-virtual")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DQT_NO_DEBUG_OUTPUT")

include(EmbedFiles)

###########################################
# Setup external dependencies
###########################################

set(QT_MIN_VERSION    "5.15.0")
include(FindQt5)

include(SetupFreetypeBrotli)

###########################################
# Add source tree
###########################################

# set(GLOBAL_NO_INTERNAL ON)
add_subdirectory(${MU_ROOT}/src/framework/global global)

# set(DRAW_NO_INTERNAL ON)
add_subdirectory(${MU_ROOT}/src/framework/draw draw)

# set(ENGRAVING_NO_INTERNAL ON)
set(ENGRAVING_NO_ACCESSIBILITY ON)
add_subdirectory(${MU_ROOT}/src/engraving engraving)

add_subdirectory(importexport)

add_executable(webmscore
    main.cpp
)

set_target_properties(engraving PROPERTIES COMPILE_FLAGS "-fPIC")
set_target_properties(global PROPERTIES COMPILE_FLAGS "-fPIC")
set_target_properties(webmscore PROPERTIES COMPILE_FLAGS "-fPIC")
set_target_properties (webmscore PROPERTIES LINK_FLAGS ${WASM_LINK_FLAGS})

target_include_directories(webmscore PUBLIC
    ${MU_ROOT}/src/framework
    ${MU_ROOT}/src/framework/global
    ${MU_ROOT}/src
    ${MU_ROOT}/src/engraving
)

target_link_libraries(webmscore
    ${QT_LIBRARIES}
    global
    draw
    engraving
    importexport
    freetype # important for supporting woff2 files
)
