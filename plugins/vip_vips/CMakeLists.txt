# vip_vips — Plugify C++ plugin: port of the Pisex VIP module "[VIP] VIPS".
#
# Pure s2sdk: no s2guard and no pisexcompat. All safety rules (map gate, slot/controller/pawn
# checks, colour tags, KV1 config, console/chat command registration) live in src/ and are shared
# by the plugin and the test through vip_vips_support.
#
#   plugify-vip_vips  the plugin shared library (entry "bin/vip_vips" -> bin/libvip_vips.so)
#   vip_vips_tests    engine-free unit tests: the KV1 reader on vip_vips.ini, the cs2-menus colour
#                     table, FormatPhrase and the bare chat word, plus the guards and the command
#                     dispatch driven through fake s2sdk bindings (build/tests/vip_vips_tests)

set(VIP_VIPS_VERSION "1.2.0")

# Binding globals the plugify module fills from the plugins declared as dependencies:
#   __s2sdk_*   (s2sdk_bindings)
#   __vip_*     (vip consumer bindings, generated into plugins/vip/include/external/plugify/include)
# In the monorepo plugins/vip is added first ("vip" < "vip_vips"), so vip_bindings already exists;
# a standalone exported repo only carries plugins/vip/include/ headers + vip.cpp and has no
# plugins/vip/CMakeLists.txt - define vip_bindings here when absent.
if(NOT TARGET vip_bindings)
    add_library(vip_bindings OBJECT
            "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include/vip/vip.cpp")
    target_include_directories(vip_bindings SYSTEM PUBLIC
            "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include"
            "${CMAKE_SOURCE_DIR}/sdk/include")
    target_compile_definitions(vip_bindings PUBLIC _GLIBCXX_USE_CXX11_ABI=1)
endif()

# The engine-free half of the plugin: formatting/colour tags, the KV1 reader, the bare chat word,
# the configuration reader, the map/client guards and the command registration. Compiled once
# into the plugin and once into the test; the __s2sdk_* binding globals are linked by the final
# targets, never by this library.
add_library(vip_vips_support STATIC
        src/text.cpp
        src/kv1.cpp
        src/chat_words.cpp
        src/config.cpp
        src/log.cpp
        src/guards.cpp
        src/commands.cpp
)
target_include_directories(vip_vips_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
# SYSTEM: warnings from the generated s2sdk/plg headers must not fail -Werror on our sources.
target_include_directories(vip_vips_support SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/sdk/include")
target_link_libraries(vip_vips_support PUBLIC plugify_sdk)
target_compile_options(vip_vips_support PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(vip_vips_support PUBLIC _GLIBCXX_USE_CXX11_ABI=1)

add_library(plugify-vip_vips SHARED
        src/plugin.cpp
        $<TARGET_OBJECTS:s2sdk_bindings>
        $<TARGET_OBJECTS:vip_bindings>
)
target_include_directories(plugify-vip_vips PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# SYSTEM: warnings from the generated vip/s2sdk/plg headers must not fail -Werror on our sources.
target_include_directories(plugify-vip_vips SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/plugins/vip/include"
        "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include"
        "${CMAKE_SOURCE_DIR}/sdk/include")
target_link_libraries(plugify-vip_vips PRIVATE
        vip_vips_support
        plugify_sdk
        -static-libstdc++ -static-libgcc
)
target_compile_options(plugify-vip_vips PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(plugify-vip_vips PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        PLUGIFY_MODULE="vip_vips"
        PLUGIFY_IS_DEBUG=0
        PLUGIFY_IS_RELEASE=1
)
set_target_properties(plugify-vip_vips PROPERTIES OUTPUT_NAME vip_vips)

if(UNIX AND NOT APPLE)
    target_link_options(plugify-vip_vips PRIVATE
            "-Wl,--version-script,${CMAKE_CURRENT_SOURCE_DIR}/sym/version_script.lds")
endif()

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/vip_vips.pplugin.in"
        "${CMAKE_CURRENT_BINARY_DIR}/vip_vips.pplugin"
)

# --- tests --------------------------------------------------------------------------------------
# vip_vips_tests: no engine. The pure half (KV1/colour tags/FormatPhrase/chat words) reads the
# production-shaped vip_vips.ini from tests/data; the guard half installs fake __s2sdk_* bindings
# and checks that no client query leaves the plugin outside a live map, that HasPawn() rejects the
# clients the Pisex lookup rejected, that PrintToChat is guarded, and that the console/chat
# sources and the bare word dispatch as they did before the rewrite.
add_executable(vip_vips_tests
        tests/vip_vips_tests.cpp
        $<TARGET_OBJECTS:s2sdk_bindings>
)
target_include_directories(vip_vips_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(vip_vips_tests SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/sdk/include")
target_link_libraries(vip_vips_tests PRIVATE
        vip_vips_support
        plugify_sdk
        -static-libstdc++ -static-libgcc
)
target_compile_options(vip_vips_tests PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(vip_vips_tests PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        VIP_VIPS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
)
set_target_properties(vip_vips_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
