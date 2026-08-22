# WP-605A: portable package metadata. Platform-native installers and Qt
# deployment are deliberately separate release work; this gate packages the
# self-contained CLI and any configured GUI target in a relocatable archive.

set(CPACK_PACKAGE_NAME "png-analyzer")
set(CPACK_PACKAGE_VENDOR "PNG Analyzer")
set(CPACK_PACKAGE_CONTACT "PNG Analyzer maintainers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "PNG structure and Deflate analyzer")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "PNG Analyzer")
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

if(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()
