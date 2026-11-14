include_guard(GLOBAL)

# Install one complete payload before wrapping it in platform-specific packages.
# License texts and build-specific dependency notices accompany every binary distribution.
set(PIP_RELEASE_LICENSE_DIR "" CACHE PATH "Additional verified dependency notices for this build")
install(FILES "${PROJECT_SOURCE_DIR}/packaging/release_readme.md"
    DESTINATION "." RENAME "README.md")
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION "share/PIPClient/licenses"
    RENAME "PIP-Client-Apache-2.0.txt")
install(FILES "${PROJECT_SOURCE_DIR}/packaging/release_readme.md"
    "${PROJECT_SOURCE_DIR}/packaging/third_party_notices.md"
    DESTINATION "share/PIPClient")
install(FILES "${PROJECT_SOURCE_DIR}/resources/fonts/ofl_barlow.txt"
    "${PROJECT_SOURCE_DIR}/resources/fonts/ofl_noto_sans_sc.txt"
    "${PROJECT_SOURCE_DIR}/resources/icons/license_lucide.txt"
    DESTINATION "share/PIPClient/licenses")
if (PIP_RELEASE_LICENSE_DIR)
    if (NOT IS_DIRECTORY "${PIP_RELEASE_LICENSE_DIR}")
        message(FATAL_ERROR "PIP_RELEASE_LICENSE_DIR must be an existing directory")
    endif()
    install(DIRECTORY "${PIP_RELEASE_LICENSE_DIR}/" DESTINATION "share/PIPClient/licenses")
endif()

set(CPACK_PACKAGE_NAME "PIPClient")
set(CPACK_PACKAGE_VENDOR "PIP")
set(CPACK_PACKAGE_VERSION "${PIP_VERSION_FULL}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "RoboMaster video and referee-system client")
set(CPACK_PACKAGE_FILE_NAME "PIPClient-${PIP_VERSION_FULL}-${CMAKE_SYSTEM_NAME}-x86_64")
if (WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)

if (WIN32)
    # Inno Setup consumes the same deployed directory as ZIP, never an incomplete build tree.
    file(TO_NATIVE_PATH "${PROJECT_SOURCE_DIR}/resources/app/variants/${PIP_APP_ICON_VARIANT}/pip_client.ico"
        PIP_INSTALLER_ICON)
    configure_file("${PROJECT_SOURCE_DIR}/packaging/windows/installer.iss.in"
        "${CMAKE_CURRENT_BINARY_DIR}/packaging/installer.iss" @ONLY)
endif()
