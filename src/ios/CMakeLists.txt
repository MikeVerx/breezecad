# BreezeCAD for iOS and iPadOS — step 2 of the port (docs/dev/IOS-PORT.md):
# the 3D view of the example part, turned with the fingers. A plain
# executable here; scripts/ios/package-app.sh puts it into BreezeCAD.app with
# Info.plist, the example and the icon (no Xcode project: the same build as
# every other platform, Ninja and CMake presets).
add_executable(breezecad IosMain.mm)
# UIKit and OpenGL ES are Objective-C(++); ARC as in platform/*.mm.
set_source_files_properties(IosMain.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(breezecad PRIVATE
    bz::core bz::document bz::kernel bz::render bz::occt_visual
    # OCCT is static on iOS: the system frameworks its toolkits use come here.
    "-framework Foundation" "-framework CoreFoundation" "-framework CoreGraphics" "-framework CoreText"
    "-framework UIKit" "-framework QuartzCore" "-framework OpenGLES")
set_target_properties(breezecad PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/ios)
bz_set_warnings(breezecad)
