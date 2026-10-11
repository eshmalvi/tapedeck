add_executable(tapedeck_stats tapedeck_stats.cpp)
target_link_libraries(tapedeck_stats PRIVATE tapedeck::tapedeck)
tapedeck_set_warnings(tapedeck_stats)

add_executable(tapedeck_replay tapedeck_replay.cpp)
target_link_libraries(tapedeck_replay PRIVATE tapedeck::tapedeck)
tapedeck_set_warnings(tapedeck_replay)

if(TAPEDECK_BUILD_TUI)
    add_executable(tapedeck_tui tapedeck_tui.cpp)
    target_link_libraries(tapedeck_tui PRIVATE tapedeck::tapedeck ftxui::screen ftxui::dom ftxui::component)
    tapedeck_set_warnings(tapedeck_tui)
endif()
