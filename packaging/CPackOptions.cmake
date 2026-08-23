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

# WP-605D: keep the relocatable archive as the default generator while
# publishing Debian metadata for the Linux-only native package gate.  The
# smoke runner selects DEB explicitly, so existing portable-package checks
# continue to produce exactly one archive.
if(UNIX AND NOT APPLE)
  set(CPACK_DEBIAN_PACKAGE_NAME "png-analyzer")
  set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "PNG Analyzer maintainers")
  set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
  set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
  set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/babybandf/PNG-Analyzer")
  set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "PNG structure and Deflate analyzer")
  set(CPACK_DEBIAN_FILE_NAME
      "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}.deb")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
endif()

# WP-605E: native installer metadata.  The smoke runner selects DragNDrop or
# NSIS explicitly; TGZ/ZIP remain the default portable generators.
if(APPLE)
  set(CPACK_DMG_VOLUME_NAME "PNG Analyzer")
  set(CPACK_DMG_FORMAT "UDZO")
endif()
if(WIN32)
  set(CPACK_NSIS_DISPLAY_NAME "PNG Analyzer")
  set(CPACK_NSIS_PACKAGE_NAME "PNG Analyzer")
  set(CPACK_NSIS_MUI_ICON
      "${CMAKE_SOURCE_DIR}/packaging/icons/png-analyzer.ico")
  set(CPACK_NSIS_MUI_UNIICON
      "${CMAKE_SOURCE_DIR}/packaging/icons/png-analyzer.ico")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_NSIS_MODIFY_PATH OFF)
endif()
