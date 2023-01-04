
# yamlcpp shadows a bunch of variables but we don't want to hear bout it
add_compile_options(-Wno-shadow)
add_subdirectory(yamlcpp)
set(YAML_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/lib/yamlcpp/include PARENT_SCOPE)
add_subdirectory(fastlz)
set(SWOC_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/lib/swoc/include PARENT_SCOPE)
