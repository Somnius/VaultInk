add_executable(GamepadTest
  GamepadTest.cpp
)

target_include_directories(GamepadTest PRIVATE
  ${REPO_ROOT}/lib/Gamepad
  ${REPO_ROOT}/lib/Gestures
)

target_link_libraries(GamepadTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GamepadTest)

# The settings keys (GamepadJson.h), with ArduinoJson at the version the firmware pins (the same declaration as
# lib/BlePageTurner/test, so it is fetched once). Only its src/ is used.
include(FetchContent)
FetchContent_Declare(arduinojson GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git GIT_TAG v7.4.2
                     SOURCE_SUBDIR no-build)
FetchContent_MakeAvailable(arduinojson)

add_executable(GamepadJsonTest
  GamepadJsonTest.cpp
)

target_include_directories(GamepadJsonTest PRIVATE
  ${REPO_ROOT}/lib/Gamepad
  ${REPO_ROOT}/lib/Gestures
)
target_include_directories(GamepadJsonTest SYSTEM PRIVATE ${arduinojson_SOURCE_DIR}/src)

target_link_libraries(GamepadJsonTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GamepadJsonTest)
