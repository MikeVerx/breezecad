add_executable(breezecad WIN32
    main.cpp
    Actions.cpp
    AppActions.cpp
    AppearanceLibrary.cpp
    Application.cpp
    AssemblyState.cpp
    Autosave.cpp
    assembly/AssemblyUi.cpp
    Backgrounds.cpp
    BetaTesting.cpp
    Format.cpp
    Language.cpp
    Materials.cpp
    Notifications.cpp
    Operations.cpp
    Session.cpp
    Settings.cpp
    Workspace.cpp
    scripting/Scripting.cpp
    scripting/DocumentCommands.cpp
    scripting/PartCommands.cpp
    scripting/SketchCommands.cpp
    scripting/SolidCommands.cpp
    scripting/TopologyCommands.cpp
    features/AppearanceEditor.cpp
    features/CopyEditors.cpp
    features/CoilEditor.cpp
    features/SketchAxis.cpp
    features/EdgeOperationEditor.cpp
    features/HoleEditor.cpp
    features/LoftEditor.cpp
    features/SweepEditor.cpp
    features/ThreadEditor.cpp
    features/PointPick.cpp
    features/ShellEditor.cpp
    features/WorkAxisEditor.cpp
    features/WorkPointEditor.cpp
    features/WorkPlaneEditor.cpp
    features/DragArrow.cpp
    features/ExtrudeEditor.cpp
    features/FeatureOperationEditor.cpp
    features/RevolveEditor.cpp
    features/SectionEditor.cpp
    features/SketchOperationEditor.cpp
    shell/Bars.cpp
    shell/Dialogs.cpp
    shell/Header.cpp
    shell/HomeScreen.cpp
    shell/LeftPanels.cpp
    shell/Ribbon.cpp
    shell/Splash.cpp
    shell/TreePanel.cpp
    sketch/DimensionLayout.cpp
    MouseNavigation.cpp
    ShortcutPresets.cpp
    sketch/SketchEditor.cpp
    sketch/SketchOverlay.cpp
    sketch/SketchText.cpp
    sketch/SketchPick.cpp
    sketch/SketchTools.cpp
    sketch/SketchToolPanel.cpp
    viewport/MarkingMenu.cpp
    viewport/Measure.cpp
    viewport/Planes.cpp
    viewport/Section.cpp
    viewport/ViewportPanel.cpp)
if(WIN32)
    # The icon in the exe (Explorer, a pinned taskbar button). Without the
    # RC language the .rc file was silently left out.
    enable_language(RC)
    target_sources(breezecad PRIVATE ${PROJECT_SOURCE_DIR}/resources/app.rc)
    # Keep main() as the entry point for a GUI (no console) application.
    if(MINGW)
        target_link_options(breezecad PRIVATE -mwindows)
    else()
        set_target_properties(breezecad PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
    endif()
endif()
target_link_libraries(breezecad PRIVATE
    bz::core bz::document bz::assembly bz::kernel bz::modules bz::render bz::ui bz::platform
    bz::imgui bz::miniz bz::json lunasvg::lunasvg glfw OpenGL::GL)
bz_set_warnings(breezecad)

# Assets next to the executable: the portable layout the app looks for first.
# A target of its own, so an edited icon arrives even when nothing relinks.
add_custom_target(bz_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:breezecad>/assets
    COMMENT "Copying assets")
add_dependencies(breezecad bz_assets)
