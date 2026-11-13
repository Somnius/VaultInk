add_executable(SshTest
  SshTerminalTest.cpp
  SshKnownHostsTest.cpp
  SshKeyTest.cpp
  SshAddressTest.cpp
  SshInputTest.cpp
  ${REPO_ROOT}/lib/Ssh/SshTerminal.cpp
  ${REPO_ROOT}/lib/Ssh/SshKnownHosts.cpp
  ${REPO_ROOT}/lib/Ssh/SshKey.cpp
  ${REPO_ROOT}/lib/Ssh/SshAddress.cpp
  ${REPO_ROOT}/lib/Ssh/SshInput.cpp
  ${REPO_ROOT}/lib/RemoteFiles/RemoteFiles.cpp
  ${REPO_ROOT}/lib/TextInput/TextInput.cpp
)

target_include_directories(SshTest PRIVATE
  ${REPO_ROOT}/lib/Ssh
  ${REPO_ROOT}/lib/RemoteFiles
  ${REPO_ROOT}/lib/TextInput
  ${REPO_ROOT}/src
)

find_package(Threads REQUIRED)

target_link_libraries(SshTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
  Threads::Threads
)

gtest_discover_tests(SshTest)

# The server list's JSON form needs ArduinoJson (the livesync tests fetch the same version).
if(NOT TARGET ArduinoJson)
  FetchContent_Declare(arduinojson GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git GIT_TAG v7.4.2
                       GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(arduinojson)
endif()

add_executable(RemoteServerJsonTest RemoteServerJsonTest.cpp)
target_include_directories(RemoteServerJsonTest PRIVATE ${REPO_ROOT}/lib/RemoteFiles)
target_link_libraries(RemoteServerJsonTest PRIVATE crosspoint_test_common ArduinoJson GTest::gtest_main)
gtest_discover_tests(RemoteServerJsonTest)
