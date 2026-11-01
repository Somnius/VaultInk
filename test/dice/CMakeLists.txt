add_executable(DiceTest
  DiceTest.cpp
  ${REPO_ROOT}/lib/Dice/Dice.cpp
)

target_include_directories(DiceTest PRIVATE
  ${REPO_ROOT}/lib/Dice
)

target_link_libraries(DiceTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DiceTest)
