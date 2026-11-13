add_executable(ScrollViewTest
  ScrollViewTest.cpp
)

target_include_directories(ScrollViewTest PRIVATE
  ${REPO_ROOT}/lib/ScrollView
)

target_link_libraries(ScrollViewTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ScrollViewTest)
