AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src")

AC_ADD_CONFIG_FILE("${CMAKE_CURRENT_LIST_DIR}/conf/mod_atlas.conf.dist")

set_property(
  GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/tests/AtlasBossSpawnQueryTest.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/tests/AtlasRunRngTest.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/tests/AtlasScalingMathTest.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/tests/AtlasAffixTest.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/tests/AtlasThemeTest.cpp")

set_property(
  GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_INCLUDES
  "${CMAKE_CURRENT_LIST_DIR}/src")
