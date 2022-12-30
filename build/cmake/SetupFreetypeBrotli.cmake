##
## FreeType v2.10.2
##
set(USE_SYSTEM_FREETYPE ON)

set(BROTLIDEC_LIBRARIES brotlidec)
set(BROTLIDEC_INCLUDE_DIRS ${MU_ROOT}/thirdparty/brotli/c/include)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(BrotliDec
    REQUIRED_VARS BROTLIDEC_INCLUDE_DIRS BROTLIDEC_LIBRARIES
    FOUND_VAR BROTLIDEC_FOUND
)

include_directories(${MU_ROOT}/thirdparty/freetype-2-10/include)

subdirs(
    ${MU_ROOT}/thirdparty/brotli
    ${MU_ROOT}/thirdparty/freetype-2-10
)

add_compile_definitions(FT_CONFIG_OPTION_USE_BROTLI)
