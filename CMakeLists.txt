cmake_minimum_required(VERSION 3.2)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set extension name here
set(TARGET_NAME spatial)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(CMAKE_CXX_STANDARD 11)

project(${TARGET_NAME})

add_definitions(-DDUCKDB_MAJOR_VERSION=${DUCKDB_MAJOR_VERSION})
add_definitions(-DDUCKDB_MINOR_VERSION=${DUCKDB_MINOR_VERSION})
add_definitions(-DDUCKDB_PATCH_VERSION=${DUCKDB_PATCH_VERSION})

# Options

# Enable network functionality (OpenSSL and GDAL's CURL based fs/drivers)
option(SPATIAL_USE_NETWORK "Enable network functionality" ON)

if(EMSCRIPTEN
   OR IOS
   OR ANDROID)
  set(SPATIAL_USE_NETWORK OFF)
endif()

include_directories(spatial/include)
add_subdirectory(spatial/src)

include_directories(spatial/third_party/yyjson/include)
add_subdirectory(spatial/third_party/yyjson)

include_directories(spatial/third_party/protozero/include)

include_directories(spatial/third_party/shapelib)
add_subdirectory(spatial/third_party/shapelib)

add_library(${EXTENSION_NAME} STATIC ${EXTENSION_SOURCES})

# Build dependencies TODO: in the future we should allow users to dynamically
# link to their own dependencies if they are building locally. If they have a
# local (compatible) GDAL installation, we should use that instead of building
# our own. (unless a static build is requested)

if(NOT EXISTS ${CMAKE_BINARY_DIR}/deps)
  execute_process(
    # Generate project
    COMMAND
      ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR} -DDUCKDB_ENABLE_DEPRECATED_API=1
      -DWASM_LOADABLE_EXTENSIONS=1 -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DOSX_BUILD_ARCH=${OSX_BUILD_ARCH}
      -DSPATIAL_USE_NETWORK=${SPATIAL_USE_NETWORK}
      -DOPENSSL_ROOT_DIR=$ENV{OPENSSL_ROOT_DIR}
      -DVCPKG_MANIFEST_DIR='${VCPKG_MANIFEST_DIR}'
      -DVCPKG_INSTALLED_DIR='${CMAKE_BINARY_DIR}/vcpkg_installed'
      -DCMAKE_TOOLCHAIN_FILE='${CMAKE_TOOLCHAIN_FILE}'
      -DVCPKG_TARGET_TRIPLET='${VCPKG_TARGET_TRIPLET}'
      -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE='${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}' -S
      ${CMAKE_CURRENT_SOURCE_DIR}/deps -B ${CMAKE_BINARY_DIR}/deps
    RESULT_VARIABLE DEPENDENCIES_GENERATE_RESULT)
  if(NOT DEPENDENCIES_GENERATE_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not generate dependencies project")
  endif()

  execute_process(
    # Build project
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/deps --config
            ${CMAKE_BUILD_TYPE} RESULT_VARIABLE DEPENDENCIES_BUILD_RESULT)
  if(NOT DEPENDENCIES_BUILD_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not build dependencies project")
  endif()
endif()

# annoyingly for expat on windows
set(CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES} MD.lib)

# Add dependencies
list(APPEND CMAKE_FIND_ROOT_PATH "${CMAKE_BINARY_DIR}/deps/local/")
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}/deps/local/")

message(STATUS "Find libraries path: '${CMAKE_PREFIX_PATH}'")

set(ZLIB_USE_STATIC_LIBS ON)
set(OPENSSL_USE_STATIC_LIBS ON)

if(EMSCRIPTEN)
  set(OPENSSL_USE_STATIC_LIBS OFF)
endif()

find_library(SQLITE3_MEMVFS memvfs)
find_package(ZLIB REQUIRED)
find_package(PROJ REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(GEOS REQUIRED)
find_package(GDAL REQUIRED)
find_package(EXPAT REQUIRED)
find_package(GeographicLib REQUIRED)

# Important: The link order matters, its the reverse order of dependency
set(EXTENSION_DEPENDENCIES
    GDAL::GDAL
    GEOS::geos_c
    PROJ::proj
    EXPAT::EXPAT
    SQLite::SQLite3
    ZLIB::ZLIB
    ${SQLITE3_MEMVFS}
    ${GeographicLib_LIBRARIES})

if(SPATIAL_USE_NETWORK)
  message(STATUS "Building with network functionality")
  message(STATUS "OpenSSL root dir hint: '$ENV{OPENSSL_ROOT_DIR}'")
  find_package(OpenSSL REQUIRED)
  find_package(CURL REQUIRED)
  list(APPEND EXTENSION_DEPENDENCIES CURL::libcurl OpenSSL::SSL OpenSSL::Crypto)
endif()

if(WIN32)
  list(APPEND EXTENSION_DEPENDENCIES wbemuuid.lib)
endif()

if((NOT EMSCRIPTEN) AND (NOT IOS))
  if(APPLE)
    find_library(CoreFoundation_Library CoreFoundation)
    find_library(SystemConfiguration_Library SystemConfiguration)
    list(APPEND EXTENSION_DEPENDENCIES ${CoreFoundation_Library}
         ${SystemConfiguration_Library})
  endif()
endif()

# Geographiclib is special
include_directories(${GeographicLib_INCLUDE_DIRS})

# Add dependencies to extension
target_link_libraries(${EXTENSION_NAME} PUBLIC ${EXTENSION_DEPENDENCIES})

# Build extensions
set(PARAMETERS "-warnings")
build_loadable_extension(${TARGET_NAME} ${PARAMETERS} ${EXTENSION_SOURCES})

target_link_libraries(${TARGET_NAME}_loadable_extension
                      ${EXTENSION_DEPENDENCIES})

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
