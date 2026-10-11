add_executable(tapedeck_bench bench_pipeline.cpp)
target_link_libraries(tapedeck_bench PRIVATE tapedeck::tapedeck benchmark::benchmark)
tapedeck_set_warnings(tapedeck_bench)
