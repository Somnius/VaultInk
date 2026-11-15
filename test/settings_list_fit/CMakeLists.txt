add_executable(SettingsListFitTest
  SettingsListFitTest.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp
)
target_include_directories(SettingsListFitTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
)
target_link_libraries(SettingsListFitTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SettingsListFitTest)
