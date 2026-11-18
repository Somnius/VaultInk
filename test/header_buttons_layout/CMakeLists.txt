add_executable(HeaderButtonsLayoutTest
  HeaderButtonsLayoutTest.cpp
)

target_link_libraries(HeaderButtonsLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HeaderButtonsLayoutTest)
