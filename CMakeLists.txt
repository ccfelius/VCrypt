cmake_minimum_required(VERSION 3.5)

# Set extension name here
set(TARGET_NAME vcrypt)
set(CMAKE_CXX_STANDARD 11)

# DuckDB's extension distribution supports vcpkg. As such, dependencies can be
# added in ./vcpkg.json and then used in cmake with find_package. Feel free to
# remove or replace with other dependencies. Note that it should also be removed
# from vcpkg.json to prevent needlessly installing it..
find_package(OpenSSL REQUIRED)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})
include_directories(src/include)
add_subdirectory(src)
include_directories(../duckdb/third_party/httplib/include)

# by now do this manually, later fix this
set(EXTENSION_SOURCES
    src/vcrypt_extension.cpp
    src/vcrypt_extension.cpp
    src/vcrypt_state.cpp
    src/etype/etype_serialization.cpp
    src/core/module.cpp
    src/core/types.cpp
    src/core/functions/scalar/encrypt.cpp
    src/core/functions/scalar/key_gen.cpp
    src/core/functions/scalar/encrypt_naive.cpp
    src/core/functions/scalar/encrypt_vectorized.cpp
    src/core/functions/scalar/decrypt_vectorized.cpp
    src/core/functions/function_data/encrypt_function_data.cpp
    src/core/functions/cast/varchar_cast.cpp
    src/core/functions/secrets/authentication.cpp
    src/core/utils/vcrypt_utils.cpp
    src/core/crypto/crypto_primitives.cpp
    src/core/functions/common.cpp)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Link OpenSSL in both the static library as the loadable extension
target_link_libraries(${EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(${LOADABLE_EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
