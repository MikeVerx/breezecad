add_executable(breezecad WIN32
    main.cpp
    Actions.cpp
    AppActions.cpp
    Application.cpp
    Autosave.cpp
    Format.cpp
    Materials.cpp
    Notifications.cpp
    Operations.cpp
    Session.cpp
    Settings.cpp
    Workspace.cpp
    features/EdgeOperationEditor.cpp
    features/HoleEditor.cpp
    features/ShellEditor.cpp
    features/ExtrudeEditor.cpp
    features/FeatureOperationEditor.cpp
    features/RevolveEditor.cpp
    features/SketchOperationEditor.cpp
    shell/Bars.cpp
    shell/Dialogs.cpp
    shell/Header.cpp
    shell/HomeScreen.cpp
    shell/LeftPanels.cpp
    shell/Ribbon.cpp
    shell/TreePanel.cpp
    sketch/DimensionLayout.cpp
    sketch/SketchEditor.cpp
    sketch/SketchOverlay.cpp
    sketch/SketchPick.cpp
    sketch/SketchTools.cpp
    viewport/ViewportPanel.cpp)
if(WIN32)
    target_sources(breezecad PRIVATE ${PROJECT_SOURCE_DIR}/resources/app.rc)
    # Keep main() as the entry point for a GUI (no console) application.
    if(MINGW)
        target_link_options(breezecad PRIVATE -mwindows)
    else()
        set_target_properties(breezecad PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
    endif()
endif()
target_link_libraries(breezecad PRIVATE
    bz::core bz::document bz::kernel bz::render bz::ui bz::platform
    bz::imgui lunasvg::lunasvg glfw OpenGL::GL)
bz_set_warnings(breezecad)

# Assets next to the executable: the portable layout the app looks for first.
add_custom_command(TARGET breezecad POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:breezecad>/assets)
