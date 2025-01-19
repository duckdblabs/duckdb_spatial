add_subdirectory(geometry)
add_subdirectory(index)
add_subdirectory(functions)
add_subdirectory(io)
add_subdirectory(util)

set(EXTENSION_SOURCES
        ${EXTENSION_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/function_builder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/geoarrow.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/module.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/types.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/optimizer_rules.cpp
        PARENT_SCOPE
)