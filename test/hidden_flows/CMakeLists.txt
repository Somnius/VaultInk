add_executable(HiddenFlowsTest
  HiddenFlowsTest.cpp
  ${REPO_ROOT}/lib/HiddenFlows/HiddenFlows.cpp
)

target_include_directories(HiddenFlowsTest PRIVATE
  ${REPO_ROOT}/lib/HiddenFlows
)

target_link_libraries(HiddenFlowsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HiddenFlowsTest)
