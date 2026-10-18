# The program's sources, shared by the desktop executable and the Android
# library (docs/dev/MOBILE-PORT.md); the entry points differ.
set(BZ_APP_SOURCES
    Actions.cpp
    AppActions.cpp
    AppearanceLibrary.cpp
    Application.cpp
    AssemblyState.cpp
    Autosave.cpp
    assembly/AssemblyUi.cpp
    Backgrounds.cpp
    BetaTesting.cpp
    CrashGuard.cpp
    Format.cpp
    NewTemplates.cpp
    Language.cpp
    Materials.cpp
    Notifications.cpp
    Operations.cpp
    Session.cpp
    Settings.cpp
    TouchNavigation.cpp
    Workspace.cpp
    scripting/Scripting.cpp
    scripting/DocumentCommands.cpp
    scripting/PartCommands.cpp
    scripting/SketchCommands.cpp
    scripting/SolidCommands.cpp
    scripting/TopologyCommands.cpp
    scripting/GenericCommands.cpp
    scripting/FeatureCommands.cpp
    scripting/WorkCommands.cpp
    scripting/ViewCommands.cpp
    scripting/AssemblyCommands.cpp
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
    shell/CrashWindow.cpp
    shell/Dialogs.cpp
    shell/SaveToLibrary.cpp
    shell/NewDocumentDialog.cpp
    shell/Header.cpp
    shell/HomeScreen.cpp
    shell/LeftPanels.cpp
    shell/Ribbon.cpp
    shell/Splash.cpp
    shell/TouchpadSettings.cpp
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
    viewport/TouchGestures.cpp
    viewport/ViewportPanel.cpp
    viewport/FaceSelection.cpp
    mobile/Cursor.cpp
    mobile/Gestures.cpp
    mobile/LayoutEditor.cpp
    mobile/MobileLayout.cpp
    mobile/MobileRibbon.cpp
    mobile/Numpad.cpp
    mobile/PhoneHome.cpp
    mobile/PhoneLibrary.cpp
    mobile/TouchInput.cpp)

if(ANDROID)
    # libbreezecad.so, run by BreezeActivity (resources/android/java): the
    # crash and splash windows are processes and windows of their own on the
    # desktop; on Android the watchdog (":crash") and the app's start do that.
    list(REMOVE_ITEM BZ_APP_SOURCES shell/CrashWindow.cpp shell/Splash.cpp)
    add_library(breezecad SHARED android/AndroidMain.cpp android/AndroidInput.cpp Application_android.cpp shell/Splash_android.cpp
                ${BZ_APP_SOURCES})
    target_link_libraries(breezecad PRIVATE
        bz::core bz::document bz::assembly bz::kernel bz::modules bz::render bz::ui bz::platform
        bz::imgui bz::miniz bz::json lunasvg::lunasvg android EGL GLESv3 log)
    # The glue's entry point is called by the system, not by our code: keep it.
    target_link_options(breezecad PRIVATE "-uANativeActivity_onCreate")
    set_target_properties(breezecad PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/android)
    bz_set_warnings(breezecad)

    # The error code for the watchdog's error screen (CrashActivity): a small
    # library, so that screen does not load OCCT.
    add_library(bzcrash SHARED android/CrashJni.cpp ${PROJECT_SOURCE_DIR}/src/core/CrashCode.cpp)
    target_include_directories(bzcrash PRIVATE ${PROJECT_SOURCE_DIR}/src)
    set_target_properties(bzcrash PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/android)
    bz_set_warnings(bzcrash)
    return()
endif()

add_executable(breezecad WIN32 main.cpp ${BZ_APP_SOURCES})
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
