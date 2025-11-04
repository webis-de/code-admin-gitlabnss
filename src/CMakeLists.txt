include(FetchContent)

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

########################################################################################################################
# PROTOCOL                                                                                                             #
########################################################################################################################
# Capnproto
FetchContent_Declare(capnproto GIT_REPOSITORY https://github.com/capnproto/capnproto.git GIT_TAG v1.0.2 EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(capnproto)
list(APPEND CMAKE_MODULE_PATH "${capnproto_SOURCE_DIR}/c++/cmake")
include(${capnproto_SOURCE_DIR}/c++/cmake/CapnProtoMacros.cmake)
# find_package(CapnProto CONFIG REQUIRED)
capnp_generate_cpp(protocolSources protocolHeaders protocol/messages.capnp)
add_library(daemonproto
    ${protocolSources}
)
target_include_directories(daemonproto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(daemonproto PUBLIC CapnProto::capnp CapnProto::capnp-rpc)

########################################################################################################################
# DAEMON                                                                                                               #
########################################################################################################################
add_executable(gitlabnssd
    config.cpp
    gitlabapi.cpp
    gitlabnssd.cpp
)
target_include_directories(gitlabnssd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_compile_features(gitlabnssd PUBLIC cxx_std_23)

target_link_libraries(gitlabnssd daemonproto)

########################################################################################################################
# NSS                                                                                                                  #
########################################################################################################################
add_library(nss_gitlab SHARED # <- This truly must be shared
    config.cpp
    gitlabapi.cpp
    nss_interface.cpp
)
set_target_properties(nss_gitlab PROPERTIES
    SOVERSION 2  # Required by GNU: https://www.gnu.org/software/libc/manual/html_mono/libc.html#NSS-Module-Names
	VERSION   ${NSSGITLAB_VERSION}
)
target_include_directories(nss_gitlab PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_compile_features(nss_gitlab PUBLIC cxx_std_23)
target_link_libraries(nss_gitlab PRIVATE daemonproto)

########################################################################################################################
# AUTHORIZED KEYS                                                                                                       #
########################################################################################################################
add_executable(authorized_keys
    authorized_keys.cpp
)
set_target_properties(authorized_keys PROPERTIES
    OUTPUT_NAME fetchgitlabkeys
)
target_link_libraries(authorized_keys daemonproto)
target_link_libraries(authorized_keys nss_gitlab)

########################################################################################################################
# DEPENDENCIES                                                                                                         #
########################################################################################################################

## spdlog
set(SPDLOG_BUILD_SHARED OFF)
FetchContent_Declare(spdlog GIT_REPOSITORY https://github.com/gabime/spdlog.git GIT_TAG v1.15.0 EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(spdlog)
target_link_libraries(nss_gitlab PUBLIC spdlog::spdlog)
target_link_libraries(gitlabnssd spdlog::spdlog)

# libcpr
FetchContent_Declare(cpr GIT_REPOSITORY https://github.com/libcpr/cpr.git GIT_TAG 1.10.5 EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(cpr)
target_link_libraries(nss_gitlab PRIVATE cpr::cpr)
target_link_libraries(gitlabnssd cpr::cpr)

# RapidJSON
option(RAPIDJSON_BUILD_DOC OFF)
option(RAPIDJSON_BUILD_EXAMPLES OFF)
option(RAPIDJSON_BUILD_TESTS OFF)
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/Tencent/rapidjson/
    GIT_TAG ab1842a
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(json)
target_link_libraries(nss_gitlab PRIVATE RapidJSON)
target_include_directories(nss_gitlab PUBLIC ${json_SOURCE_DIR}/include)
target_link_libraries(gitlabnssd RapidJSON)
target_include_directories(gitlabnssd PUBLIC ${json_SOURCE_DIR}/include)

# toml++
FetchContent_Declare(tomlpp GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git GIT_TAG v3.4.0 EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(tomlpp)
target_compile_definitions(nss_gitlab PRIVATE TOML_EXCEPTIONS=0)
target_link_libraries(nss_gitlab PRIVATE tomlplusplus::tomlplusplus)
target_compile_definitions(gitlabnssd PRIVATE TOML_EXCEPTIONS=0)
target_link_libraries(gitlabnssd tomlplusplus::tomlplusplus)

# cpp-lru-cache
FetchContent_Declare(lrucache GIT_REPOSITORY https://github.com/lamerman/cpp-lru-cache.git GIT_TAG de1c4a03569bf3bd540e7f55ab5c2961411dbe22 EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(lrucache)
target_include_directories(gitlabnssd PUBLIC ${lrucache_SOURCE_DIR}/include)