cmake_minimum_required(VERSION 3.16)

project(aliyun_sls_fluent_bit_plugin LANGUAGES C CXX)

option(ALIYUN_SLS_BUILD_TESTS "Build unit tests" ON)
option(ALIYUN_SLS_BUILD_EXAMPLES "Build examples" ON)
option(ALIYUN_SLS_ENABLE_CURL "Build the default libcurl transport" ON)
option(ALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN "Build the Fluent Bit output plugin (.so)" OFF)

if(ALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN)
    set(ALIYUN_SLS_ENABLE_CURL OFF CACHE BOOL "" FORCE)
endif()

# Path to a Fluent Bit >= 3.0 source tree, used only for the plugin build. Its
# build directory (with the generated include/fluent-bit/flb_info.h) may be
# provided separately via FLUENT_BIT_BUILD_DIR; it defaults to <src>/build.
set(FLUENT_BIT_SRC "" CACHE PATH "Fluent Bit source tree for the output plugin")
set(FLUENT_BIT_BUILD_DIR "" CACHE PATH "Fluent Bit build tree (defaults to <src>/build)")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(aliyun_sls_client
    src/sls_batch.cpp
    src/sls_client.cpp
    src/sls_compress.cpp
    src/sls_crypto.cpp
    src/sls_http.cpp
    src/sls_protobuf.cpp
    src/sls_c_api.cpp
    third_party/lz4/lz4.c
)

target_include_directories(aliyun_sls_client
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lz4
)

if(APPLE)
    target_link_libraries(aliyun_sls_client PUBLIC "-framework Security")
else()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(aliyun_sls_client PUBLIC OpenSSL::Crypto)
endif()

if(ALIYUN_SLS_ENABLE_CURL)
    find_package(CURL REQUIRED)
    target_compile_definitions(aliyun_sls_client PUBLIC ALIYUN_SLS_ENABLE_CURL=1)
    target_link_libraries(aliyun_sls_client PUBLIC CURL::libcurl)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(aliyun_sls_client PRIVATE -Wall -Wextra -Wpedantic)
    set_source_files_properties(third_party/lz4/lz4.c PROPERTIES COMPILE_FLAGS "-w")
endif()

if(ALIYUN_SLS_BUILD_EXAMPLES)
    add_executable(putlogs_smoke examples/putlogs_smoke.cpp)
    target_link_libraries(putlogs_smoke PRIVATE aliyun_sls_client)
endif()

if(ALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN)
    if(NOT FLUENT_BIT_SRC)
        message(FATAL_ERROR
            "ALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN=ON requires -DFLUENT_BIT_SRC=<fluent-bit source tree>")
    endif()
    if(FLUENT_BIT_BUILD_DIR)
        set(_flb_build "${FLUENT_BIT_BUILD_DIR}")
    else()
        set(_flb_build "${FLUENT_BIT_SRC}/build")
    endif()

    # Bundled dependencies shipped inside the Fluent Bit source tree.
    # Note: Fluent Bit generates headers into both the source tree and the build
    # tree. Monkey headers may be generated below <build>/include or below
    # <build>/lib/monkey/include depending on how Fluent Bit was configured.
    # Auto-detect c-ares version directory
    file(GLOB _cares_dir "${FLUENT_BIT_SRC}/lib/c-ares-*")
    file(GLOB _cares_build_dir "${_flb_build}/lib/c-ares-*")

    set(_flb_includes
        # Fluent Bit core
        ${FLUENT_BIT_SRC}/include
        ${_flb_build}/include
        ${_flb_build}/include/monkey
        ${_flb_build}/lib/monkey/include
        ${_flb_build}/lib/monkey/include/monkey
        # Bundled libraries
        ${FLUENT_BIT_SRC}/lib/msgpack-c/include
        ${FLUENT_BIT_SRC}/lib/cmetrics/include
        ${FLUENT_BIT_SRC}/lib/cfl/include
        ${FLUENT_BIT_SRC}/lib/cfl/lib/xxhash
        ${FLUENT_BIT_SRC}/lib/monkey/include
        ${FLUENT_BIT_SRC}/lib/monkey/include/monkey
        ${FLUENT_BIT_SRC}/lib/flb_libco
        ${FLUENT_BIT_SRC}/lib/chunkio/include
        ${FLUENT_BIT_SRC}/lib/ctraces/include
        ${FLUENT_BIT_SRC}/lib/ctraces/lib/mpack/src
        ${FLUENT_BIT_SRC}/lib/cprofiles/include
        ${FLUENT_BIT_SRC}/lib/nghttp2/lib/includes
        ${FLUENT_BIT_SRC}/lib/fluent-otel-proto/include
        ${FLUENT_BIT_SRC}/lib/lwrb/lwrb/src/include
    )
    # c-ares headers (source + generated ares_build.h in build tree)
    if(_cares_dir)
        list(APPEND _flb_includes "${_cares_dir}/include" "${_cares_dir}")
    endif()
    if(_cares_build_dir)
        list(APPEND _flb_includes "${_cares_build_dir}")
    endif()
    # Monkey generated headers such as mk_info.h, mk_env.h, and
    # mk_core/mk_core_info.h can live under different build-tree roots.
    foreach(_monkey_include
            "${_flb_build}/include"
            "${_flb_build}/include/monkey"
            "${_flb_build}/lib/monkey/include"
            "${_flb_build}/lib/monkey/include/monkey")
        if(EXISTS "${_monkey_include}")
            list(APPEND _flb_includes "${_monkey_include}")
        endif()
    endforeach()
    # Build-tree generated headers for sub-libraries
    foreach(_sublib cfl cmetrics ctraces cprofiles chunkio)
        if(EXISTS "${_flb_build}/lib/${_sublib}/include")
            list(APPEND _flb_includes "${_flb_build}/lib/${_sublib}/include")
        endif()
    endforeach()
    # nghttp2 generated version header
    if(EXISTS "${_flb_build}/lib/nghttp2/lib/includes")
        list(APPEND _flb_includes "${_flb_build}/lib/nghttp2/lib/includes")
    endif()

    # Built as a shared object so it can be loaded with `-e out_aliyun_sls.so`;
    # to build it in-tree, drop these sources into plugins/out_aliyun_sls/ and
    # register the target with the Fluent Bit CMake instead.
    add_library(flb-out_aliyun_sls SHARED
        src/fluent_bit_out_aliyun_sls_plugin.c
    )
    set_target_properties(flb-out_aliyun_sls PROPERTIES PREFIX "" OUTPUT_NAME "out_aliyun_sls")
    target_compile_definitions(flb-out_aliyun_sls PRIVATE ALIYUN_SLS_BUILD_FLUENT_BIT_PLUGIN=1)
    target_include_directories(flb-out_aliyun_sls PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${_flb_includes}
    )
    target_link_libraries(flb-out_aliyun_sls PRIVATE aliyun_sls_client)
endif()

if(ALIYUN_SLS_BUILD_TESTS)
    enable_testing()
    add_executable(test_protobuf tests/test_protobuf.cpp)
    target_include_directories(test_protobuf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_protobuf PRIVATE aliyun_sls_client)
    add_test(NAME test_protobuf COMMAND test_protobuf)

    add_executable(test_crypto tests/test_crypto.cpp)
    target_include_directories(test_crypto PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_crypto PRIVATE aliyun_sls_client)
    add_test(NAME test_crypto COMMAND test_crypto)

    add_executable(test_client tests/test_client.cpp)
    target_include_directories(test_client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lz4
    )
    target_link_libraries(test_client PRIVATE aliyun_sls_client)
    add_test(NAME test_client COMMAND test_client)

    add_executable(test_batch tests/test_batch.cpp)
    target_include_directories(test_batch PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(test_batch PRIVATE aliyun_sls_client)
    add_test(NAME test_batch COMMAND test_batch)
endif()
