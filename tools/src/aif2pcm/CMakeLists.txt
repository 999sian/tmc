file(GLOB_RECURSE sources *.c)

add_executable(aif2pcm ${sources})
target_include_directories(aif2pcm PRIVATE .)
target_link_libraries(aif2pcm PRIVATE m)

add_library(aif2pcm_embedded STATIC ${sources})
target_compile_definitions(aif2pcm_embedded PRIVATE main=aif2pcm_main)
if(UNIX)
    target_link_libraries(aif2pcm_embedded PUBLIC m)
endif()

install(TARGETS aif2pcm RUNTIME DESTINATION bin)
