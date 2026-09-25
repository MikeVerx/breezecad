#include "Thumbnails.h"
#include "occt/OcctView.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>

namespace BreezeCAD::UI::Home {

std::string GetThumbnailPath(const std::vector<std::string>& thumbs, int docIndex) {
    if (docIndex < 0 || docIndex >= (int)thumbs.size()) return "";
    return thumbs[docIndex];
}

void AddRecentFile(std::vector<std::string>& recent, const std::string& path) {
    if (path.empty()) return;
    recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
    recent.insert(recent.begin(), path);
    if (recent.size() > 10) recent.resize(10);
}

void CaptureCurrentThumbnail(OcctView& view, int activeDocIndex,
                             std::vector<std::string>& thumbs) {
    if (activeDocIndex < 0) return;

    std::string dir = "D:/test_breezecad/thumbs/";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    char filename[64];
    std::snprintf(filename, sizeof(filename), "thumb_%d.bmp", activeDocIndex);
    std::string path = dir + filename;

    if (view.CaptureToPixmap(320, 240, path)) {
        if ((int)thumbs.size() <= activeDocIndex) {
            thumbs.resize(activeDocIndex + 1);
        }
        thumbs[activeDocIndex] = path;
        std::cout << "Thumbnail saved: " << path << "\n";
    }
}

} // namespace BreezeCAD::UI::Home
