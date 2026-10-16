# The Android app: libbreezecad.so, loaded by the system's NativeActivity
# (resources/android/AndroidManifest.xml.in). scripts/android/package-apk.sh
# puts it into the APK with OCCT, FreeType and the C++ runtime.
set(BZ_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)
add_library(bz_android_glue STATIC ${BZ_GLUE_DIR}/android_native_app_glue.c)
target_include_directories(bz_android_glue SYSTEM PUBLIC ${BZ_GLUE_DIR})

add_library(breezecad SHARED AndroidMain.cpp)
target_link_libraries(breezecad PRIVATE bz::core bz::document bz::kernel bz::render bz_android_glue
                                        android EGL GLESv3 log)
# The glue's entry point is called by the system, not by our code: keep it.
target_link_options(breezecad PRIVATE "-uANativeActivity_onCreate")
set_target_properties(breezecad PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/android)
bz_set_warnings(breezecad)
