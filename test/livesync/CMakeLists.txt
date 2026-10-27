# mbedtls: the copy ESP-IDF ships (what the device builds with), else the same release from GitHub.
file(GLOB _espidf_mbedtls LIST_DIRECTORIES true
     "$ENV{HOME}/.platformio-ci/packages/framework-espidf/components/mbedtls/mbedtls"
     "$ENV{HOME}/.platformio/packages/framework-espidf/components/mbedtls/mbedtls")
include(FetchContent)
if(_espidf_mbedtls)
  list(GET _espidf_mbedtls 0 MBEDTLS_DIR)
else()
  FetchContent_Declare(mbedtls_src GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git GIT_TAG v3.6.6
                       GIT_SHALLOW TRUE GIT_SUBMODULES "")
  FetchContent_Populate(mbedtls_src)
  set(MBEDTLS_DIR ${mbedtls_src_SOURCE_DIR})
endif()

file(GLOB MBEDTLS_SOURCES ${MBEDTLS_DIR}/library/*.c)  # its default config needs most of the library
add_library(livesync_mbedtls STATIC ${MBEDTLS_SOURCES})
target_include_directories(livesync_mbedtls PUBLIC ${MBEDTLS_DIR}/include)
target_compile_options(livesync_mbedtls PRIVATE -w)

add_executable(LiveSyncCryptoTest
  LiveSyncCryptoTest.cpp
  ${REPO_ROOT}/lib/LiveSync/LiveSyncCrypto.cpp
)
target_include_directories(LiveSyncCryptoTest PRIVATE ${REPO_ROOT}/lib/LiveSync)
target_link_libraries(LiveSyncCryptoTest PRIVATE livesync_mbedtls GTest::gtest_main)
gtest_discover_tests(LiveSyncCryptoTest)

FetchContent_Declare(arduinojson GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git GIT_TAG v7.4.2
                     GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(arduinojson)

add_executable(LiveSyncPullTest
  LiveSyncPullTest.cpp
  ${REPO_ROOT}/lib/LiveSync/LiveSyncCrypto.cpp
  ${REPO_ROOT}/lib/LiveSync/LiveSyncPull.cpp
)
target_include_directories(LiveSyncPullTest PRIVATE ${REPO_ROOT}/lib/LiveSync)
target_compile_definitions(LiveSyncPullTest PRIVATE LIVESYNC_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixture"
                           DEMO_VAULT_DIR="${REPO_ROOT}/demo-vault/VaultInk Demo")
target_link_libraries(LiveSyncPullTest PRIVATE livesync_mbedtls ArduinoJson GTest::gtest_main)
gtest_discover_tests(LiveSyncPullTest)

add_executable(LiveSyncEngineTest
  LiveSyncEngineTest.cpp
  ${REPO_ROOT}/lib/LiveSync/LiveSyncCrypto.cpp
  ${REPO_ROOT}/lib/LiveSync/LiveSyncPull.cpp
  ${REPO_ROOT}/lib/LiveSync/LiveSyncEngine.cpp
)
target_include_directories(LiveSyncEngineTest PRIVATE ${REPO_ROOT}/lib/LiveSync)
target_compile_definitions(LiveSyncEngineTest PRIVATE LIVESYNC_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixture"
                           DEMO_VAULT_DIR="${REPO_ROOT}/demo-vault/VaultInk Demo")
target_link_libraries(LiveSyncEngineTest PRIVATE livesync_mbedtls ArduinoJson GTest::gtest_main)
gtest_discover_tests(LiveSyncEngineTest)
