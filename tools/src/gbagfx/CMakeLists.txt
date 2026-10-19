file(GLOB_RECURSE sources *.c)

set(CMAKE_FIND_FRAMEWORK LAST)

find_package(PNG REQUIRED)

# TODO Makefile had a debug build with -DDEBUG
add_executable(gbagfx ${sources})
target_include_directories(gbagfx PRIVATE .)
target_link_libraries(gbagfx PRIVATE PNG::PNG)

# Keep the standalone entry point while providing the in-process converter.
add_library(gbagfx_embedded STATIC ${sources})
target_compile_definitions(gbagfx_embedded PRIVATE main=gbagfx_main)
target_link_libraries(gbagfx_embedded PUBLIC PNG::PNG)

install(TARGETS gbagfx RUNTIME DESTINATION bin)
