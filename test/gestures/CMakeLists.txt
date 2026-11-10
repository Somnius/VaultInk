add_executable(GesturesTest
  GesturesTest.cpp
)

target_include_directories(GesturesTest PRIVATE
  ${REPO_ROOT}/lib/Gestures
)

target_link_libraries(GesturesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GesturesTest)
