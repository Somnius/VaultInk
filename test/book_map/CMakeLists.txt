add_executable(BookMapTest
  BookMapTest.cpp
  ${REPO_ROOT}/lib/BookMap/BookMap.cpp
  ${REPO_ROOT}/lib/HiddenFlows/HiddenFlows.cpp
)

target_include_directories(BookMapTest PRIVATE
  ${REPO_ROOT}/lib/BookMap
  ${REPO_ROOT}/lib/HiddenFlows
)

target_link_libraries(BookMapTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookMapTest)
