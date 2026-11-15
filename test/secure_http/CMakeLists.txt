# The HTTP client every POST and PUT of the firmware goes through (freeink-sdk), over a fake socket.
add_executable(SecureHttpWriteTest SecureHttpWriteTest.cpp)
target_include_directories(SecureHttpWriteTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/freeink-sdk/libs/network/SecureNet/include
)
target_link_libraries(SecureHttpWriteTest PRIVATE GTest::gtest_main)
gtest_discover_tests(SecureHttpWriteTest)
