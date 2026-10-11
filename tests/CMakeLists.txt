add_executable(tapedeck_tests
    test_core.cpp
    test_decoder.cpp
    test_frame_stream.cpp
    test_order_book.cpp
)
target_link_libraries(tapedeck_tests PRIVATE tapedeck::tapedeck GTest::gtest_main)
tapedeck_set_warnings(tapedeck_tests)
# Designated initialisers with partial member lists are the clearest way to build
# test messages; silence the pedantic warning about the members we leave defaulted.
target_compile_options(tapedeck_tests PRIVATE -Wno-missing-field-initializers)

include(GoogleTest)
gtest_discover_tests(tapedeck_tests)
