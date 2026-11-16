add_executable(PanelAfterMenuTest PanelAfterMenuTest.cpp)

target_include_directories(PanelAfterMenuTest PRIVATE ../../src)

target_link_libraries(PanelAfterMenuTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PanelAfterMenuTest)
