add_executable(SudokuTest
  SudokuTest.cpp
  ${REPO_ROOT}/lib/Sudoku/Sudoku.cpp
)

target_include_directories(SudokuTest PRIVATE
  ${REPO_ROOT}/lib/Sudoku
)

target_link_libraries(SudokuTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SudokuTest)
