
set(MODULE importexport)

set(MODULE_SRC
    ${MU_ROOT}/src/project/internal/notationreadersregister.cpp
    ${MU_ROOT}/src/project/internal/notationreadersregister.h
    ${MU_ROOT}/src/project/internal/notationwritersregister.cpp
    ${MU_ROOT}/src/project/internal/notationwritersregister.h
)

add_subdirectory(${MU_ROOT}/src/importexport/guitarpro guitarpro)
set(MODULE_LINK ${MODULE_LINK} iex_guitarpro)

include(SetupModule)
