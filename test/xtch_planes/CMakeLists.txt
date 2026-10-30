add_executable(XtchPlanesTest XtchPlanesTest.cpp)
target_include_directories(XtchPlanesTest PRIVATE ${REPO_ROOT}/lib/GfxRenderer)
target_link_libraries(XtchPlanesTest PRIVATE GTest::gtest_main)
gtest_discover_tests(XtchPlanesTest)
