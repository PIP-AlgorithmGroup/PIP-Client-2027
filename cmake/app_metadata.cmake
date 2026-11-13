include_guard(GLOBAL)

include(GNUInstallDirs)

# CMake requires language enablement at file scope. Keep RC inside the platform boundary,
# so Linux builds never probe Windows SDK tools or parse Windows resource scripts.
if (WIN32)
    enable_language(RC)
endif()

# Attach bundled runtime icons and native application metadata to an existing Qt executable.
# PROJECT_VERSION is the sole version source. Generated files live in the caller's build tree;
# invoking this once per executable is required, after qt_add_executable/qt_standard_project_setup.
function(pip_configure_app_metadata target)
    if (NOT TARGET "${target}")
        message(FATAL_ERROR "pip_configure_app_metadata requires an existing target: ${target}")
    endif()
    get_target_property(pip_has_metadata "${target}" PIP_HAS_APP_METADATA)
    if (pip_has_metadata)
        message(FATAL_ERROR "Application metadata was already attached to ${target}")
    endif()
    get_filename_component(pip_source_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    set(PIP_APP_ICON_VARIANT "receiver" CACHE STRING "Application icon: receiver, viewlink, circuit, signal")
    set_property(CACHE PIP_APP_ICON_VARIANT PROPERTY STRINGS receiver viewlink circuit signal)
    if (NOT PIP_APP_ICON_VARIANT MATCHES "^(receiver|viewlink|circuit|signal)$")
        message(FATAL_ERROR "Unknown application icon variant: ${PIP_APP_ICON_VARIANT}")
    endif()
    set(pip_icon_directory "${pip_source_root}/resources/app/variants/${PIP_APP_ICON_VARIANT}")
    set(pip_generated_directory "${CMAKE_CURRENT_BINARY_DIR}/app_metadata/${target}")
    file(MAKE_DIRECTORY "${pip_generated_directory}")
    get_target_property(PIP_EXECUTABLE_NAME "${target}" OUTPUT_NAME)
    if (NOT PIP_EXECUTABLE_NAME)
        set(PIP_EXECUTABLE_NAME "${target}")
    endif()
    if (NOT PIP_EXECUTABLE_NAME MATCHES "^[A-Za-z0-9_.-]+$")
        message(FATAL_ERROR "Application metadata requires a portable executable name")
    endif()

    # Runtime QIcon uses PNGs so Linux/X11 icons do not depend on an SVG image plugin.
    set(pip_icon_files "${pip_icon_directory}/pip_client.svg")
    foreach(pip_size 16 24 32 48 64 128 256 512)
        list(APPEND pip_icon_files "${pip_icon_directory}/pip_client_${pip_size}.png")
    endforeach()
    qt_add_resources("${target}" "${target}_application_icon"
        PREFIX "/resources/app"
        BASE "${pip_icon_directory}"
        FILES ${pip_icon_files})
    target_compile_definitions("${target}" PRIVATE PIP_DESKTOP_ID="org.pip.PIPClient")

    if (WIN32)
        # Native VERSIONINFO has four 16-bit numeric components, independent of display text.
        set(pip_version_components "")
        foreach(pip_component MAJOR MINOR PATCH TWEAK)
            set(pip_value "${PROJECT_VERSION_${pip_component}}")
            if (pip_value STREQUAL "")
                set(pip_value 0)
            endif()
            if (NOT pip_value MATCHES "^[0-9]+$" OR pip_value GREATER 65535)
                message(FATAL_ERROR "PROJECT_VERSION must fit four Windows 16-bit components")
            endif()
            list(APPEND pip_version_components "${pip_value}")
        endforeach()
        list(JOIN pip_version_components "," PIP_FILE_VERSION)
        file(TO_CMAKE_PATH "${pip_icon_directory}/pip_client.ico" PIP_ICON_PATH)
        configure_file("${pip_source_root}/packaging/windows/app.rc.in"
            "${pip_generated_directory}/app.rc" @ONLY)
        # RC scanners are not uniform across generators; a changed ICO must refresh the PE icon.
        set_property(SOURCE "${pip_generated_directory}/app.rc" APPEND PROPERTY OBJECT_DEPENDS
            "${pip_icon_directory}/pip_client.ico")
        target_sources("${target}" PRIVATE "${pip_generated_directory}/app.rc")
    elseif (UNIX AND NOT APPLE)
        configure_file("${pip_source_root}/packaging/org.pip.PIPClient.desktop.in"
            "${pip_generated_directory}/org.pip.PIPClient.desktop" @ONLY)
        install(FILES "${pip_generated_directory}/org.pip.PIPClient.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
        foreach(pip_size 16 24 32 48 64 128 256 512)
            install(FILES "${pip_icon_directory}/pip_client_${pip_size}.png"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${pip_size}x${pip_size}/apps"
                RENAME "org.pip.PIPClient.png")
        endforeach()
        install(FILES "${pip_icon_directory}/pip_client.svg"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
            RENAME "org.pip.PIPClient.svg")
    endif()
    set_target_properties("${target}" PROPERTIES PIP_HAS_APP_METADATA TRUE)
endfunction()
