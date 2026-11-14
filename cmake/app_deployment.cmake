include_guard(GLOBAL)

include(GNUInstallDirs)

# Generate install-time Qt/QML deployment rules for an existing application target.
# Windows keeps the selected Qt kit immutable: verified Qt 6.12 windeployqt resolves every Qt6*.dll
# through one QT_INSTALL_BINS directory, even when an imported module lives in another prefix.
# A private install-time snapshot supplies that directory; no dependency errors are ignored.
# Linux uses Qt's public deployment path and resolves external QtCore libraries from the kit.
function(pip_configure_app_deployment target)
    if (NOT TARGET "${target}")
        message(FATAL_ERROR "pip_configure_app_deployment requires an existing target: ${target}")
    endif()
    get_target_property(pip_has_deployment "${target}" PIP_HAS_APP_DEPLOYMENT)
    if (pip_has_deployment)
        message(FATAL_ERROR "Application deployment was already configured for ${target}")
    endif()

    if (WIN32)
        if (NOT TARGET Qt6::Mqtt OR NOT TARGET Qt6::qtpaths)
            message(FATAL_ERROR "Windows deployment requires Qt6::Mqtt and Qt6::qtpaths")
        endif()
        set(PIP_DEPLOY_BUILD_ROOT "${CMAKE_CURRENT_BINARY_DIR}")
        set(PIP_DEPLOY_VIEW_ROOT
            "${CMAKE_CURRENT_BINARY_DIR}/app_deployment/${target}/$<CONFIG>/view")
        set(PIP_DEPLOY_QT_BIN "$<TARGET_FILE_DIR:Qt6::Core>")
        set(PIP_DEPLOY_QT_CORE_DLL "$<TARGET_FILE:Qt6::Core>")
        set(PIP_DEPLOY_QTPATHS "$<TARGET_FILE:Qt6::qtpaths>")
        set(PIP_DEPLOY_MQTT_DLL "$<TARGET_FILE:Qt6::Mqtt>")
        set(pip_prepare_template
            "${CMAKE_CURRENT_BINARY_DIR}/app_deployment/${target}/prepare_view.in.cmake")
        configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/prepare_deploy_view.cmake.in"
            "${pip_prepare_template}" @ONLY)
        set(pip_prepare_script
            "${CMAKE_CURRENT_BINARY_DIR}/app_deployment/${target}/prepare_view_$<CONFIG>.cmake")
        file(GENERATE OUTPUT "${pip_prepare_script}" INPUT "${pip_prepare_template}")
        install(SCRIPT "${pip_prepare_script}")

        # Explicitly deploy the exact imported binary; module metadata is absent from base kits.
        install(FILES "$<TARGET_FILE:Qt6::Mqtt>" DESTINATION "${CMAKE_INSTALL_BINDIR}")
        qt_generate_deploy_qml_app_script(TARGET "${target}" OUTPUT_SCRIPT pip_deploy_script
            DEPLOY_TOOL_OPTIONS --qtpaths "${PIP_DEPLOY_VIEW_ROOT}/bin/qtpaths.exe")
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # The kit's default deployment can select only XCB. Include both desktop backends
        # explicitly, plus offscreen for isolated package validation; Qt collects their deps.
        qt_generate_deploy_qml_app_script(TARGET "${target}" OUTPUT_SCRIPT pip_deploy_script
            INCLUDE_PLUGINS qxcb qwayland qoffscreen)
    else()
        qt_generate_deploy_qml_app_script(TARGET "${target}" OUTPUT_SCRIPT pip_deploy_script)
    endif()
    install(SCRIPT "${pip_deploy_script}")
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # The official SDK may place ICU beside lib/, outside QtCore's ELF RUNPATH. Qt's
        # generic deploy tool does not search that directory or fail on unresolved libraries.
        # Resolve the selected kit's actual dependencies rather than fixing an ICU version.
        set(PIP_DEPLOY_QT_CORE "$<TARGET_FILE:Qt6::Core>")
        set(PIP_DEPLOY_QT_LIB_DIR "$<TARGET_FILE_DIR:Qt6::Core>")
        set(pip_linux_template
            "${CMAKE_CURRENT_BINARY_DIR}/app_deployment/${target}/linux_runtime.in.cmake")
        configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/deploy_linux_runtime.cmake.in"
            "${pip_linux_template}" @ONLY)
        set(pip_linux_script
            "${CMAKE_CURRENT_BINARY_DIR}/app_deployment/${target}/linux_runtime_$<CONFIG>.cmake")
        file(GENERATE OUTPUT "${pip_linux_script}" INPUT "${pip_linux_template}")
        install(SCRIPT "${pip_linux_script}")
    endif()
    set_target_properties("${target}" PROPERTIES PIP_HAS_APP_DEPLOYMENT TRUE)
endfunction()
