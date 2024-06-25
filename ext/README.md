External build extension directory
----------------------------------

Placing directories in this directoy will automatically include them in the build.  If you
want to include additional plugins or cripts bundles in your build, you can do so using this
mechanism.

Below is an example CMakeFiles.txt to include an additional cripts bundle for your cripts to use.

```
# Include additional source files with the feature implementation
cripts_sources(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/MyBundle/Feature1.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/MyBundle/Feature2.cc
)
# Add the local include directories
cripts_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include>)
# Its even possible to link additional libraries should your bundle need
cripts_link_libraries(yaml-cpp::yaml-cpp)

# finally, install the additional bundle headers that your cripts
# can include to access the new features.
cripts_install_bundle_headers(MyBundle
  ${CMAKE_CURRENT_SOURCE_DIR}/include/cripts/MyBundle/Feature1.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/cripts/MyBundle/Feature1.hpp
)
```


