#include "platform/SystemTheme.h"

#include <cstdlib>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#elif defined(__ANDROID__)
#include "platform/Android.h"
#include <android/configuration.h>
#include <android_native_app_glue.h>
#endif

namespace bz::platform {

bool SystemPrefersDark() {
#if defined(_WIN32)
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER,
                                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return status == ERROR_SUCCESS && value == 0;
#elif defined(__APPLE__)
    bool dark = false;
    if (CFStringRef style = static_cast<CFStringRef>(
            CFPreferencesCopyAppValue(CFSTR("AppleInterfaceStyle"), kCFPreferencesAnyApplication))) {
        dark = CFStringCompare(style, CFSTR("Dark"), kCFCompareCaseInsensitive) == kCFCompareEqualTo;
        CFRelease(style);
    }
    return dark;
#elif defined(__ANDROID__)
    android_app* app = android::App();
    return app && app->config && AConfiguration_getUiModeNight(app->config) == ACONFIGURATION_UI_MODE_NIGHT_YES;
#else
    // GTK desktops: a theme name ending in "-dark" is the common convention.
    const char* gtk = std::getenv("GTK_THEME");
    return gtk && std::string(gtk).find("dark") != std::string::npos;
#endif
}

} // namespace bz::platform
