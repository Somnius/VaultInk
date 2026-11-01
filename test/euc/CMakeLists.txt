# Every lib/Euc/*.cpp and test/euc/*Test.cpp: agents add files here without touching any CMakeLists.
file(GLOB EUC_SOURCES ${REPO_ROOT}/lib/Euc/*.cpp)
# Factory.cpp needs every brand, so the per-brand test runs leave it out until all exist.
list(FILTER EUC_SOURCES EXCLUDE REGEX "Factory\\.cpp$")
file(GLOB EUC_TESTS ${CMAKE_CURRENT_SOURCE_DIR}/*Test.cpp)
# EUC_MINE: a regex of file name prefixes, e.g. -DEUC_MINE="Begode|Veteran": builds only those files, so agents working in parallel
# do not trip over each other's half-written files.
if(EUC_MINE)
  list(FILTER EUC_SOURCES INCLUDE REGEX "/(${EUC_MINE})[^/]*\\.cpp$")
  list(FILTER EUC_TESTS INCLUDE REGEX "/(${EUC_MINE})[^/]*Test\\.cpp$")
endif()
add_executable(EucTest ${EUC_TESTS} ${EUC_SOURCES})
target_include_directories(EucTest PRIVATE ${REPO_ROOT}/lib/Euc)
target_link_libraries(EucTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(EucTest)
