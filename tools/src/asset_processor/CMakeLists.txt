file(GLOB_RECURSE sources *.cpp)

add_executable(asset_processor ${sources})
target_include_directories(asset_processor PRIVATE .)
target_link_libraries(asset_processor PRIVATE project_settings)
find_package(Threads REQUIRED)
target_link_libraries(asset_processor PRIVATE fmt::fmt nlohmann_json::nlohmann_json filesystem util
    gbagfx_embedded aif2pcm_embedded Threads::Threads)

install(TARGETS asset_processor RUNTIME DESTINATION bin)
