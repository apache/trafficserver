External build extension directory
----------------------------------

Placing directories in this directoy will automatically include them in the build.  If you
want to include additional plugins or cripts bundles in your build, you can do so using this
mechanism.

Below is an example CMakeLists.txt to include an additional Cripts modules and
bundles for your Cripts to use. This would go into a `ext/cripts` subdirectory.

```
cripts_sources(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Company/Module1.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Company/Module2.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/CompanyBundles/Bundle1.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/CompanyBundles/Bundle2.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/CompanyBundles/YamlBundle.cc
)

cripts_include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Only because the YamlBundle needs YAML-CPP
cripts_link_libraries(yaml-cpp::yaml-cpp)

cripts_install_bundle_headers(Company
  ${CMAKE_CURRENT_SOURCE_DIR}/include/cripts/Company/Module1.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/cripts/Company/Module2.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/cripts/Company/Module3.h
)

cripts_install_bundle_headers(CompanyBundles
  ${CMAKE_CURRENT_SOURCE_DIR}/include/cripts/CompanyBundles/Bundle1.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/cripts/CompanyBundles/Bundle2.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/cripts/CompanyBundles/YamlBundle.hpp
)
```
