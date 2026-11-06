#include "app/shell/FilePreviews.h"

#include "core/Image.h"
#include "core/PathText.h"
#include "document/BzcFile.h"
#include "ui/Gl.h"

#include <chrono>
#include <future>
#include <optional>
#include <string>
#include <unordered_map>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F   // OpenGL 1.2; Windows headers stop at 1.1
#endif

namespace bz::app {
namespace {

constexpr int kSide = 320;   ///< the largest side kept (a card is about 200 px; sharp on 150 % screens)
using Clock = std::chrono::steady_clock;

struct Entry {
    std::filesystem::file_time_type stamp{};
    bool          read = false;     ///< this stamp was read (with or without a picture)
    Clock::time_point checked{};    ///< the file's date was looked at (not every frame)
    FilePreview   picture;
};

std::unordered_map<std::string, Entry> gEntries;

// One file in the background at a time.
struct Job {
    std::string key;
    std::filesystem::file_time_type stamp{};
    std::future<std::optional<Image>> result;
};
std::optional<Job> gJob;
Clock::time_point gAsked{};

/// file_clock -> ms since 1970, by the two clocks' "now" (clock_cast is missing in libc++).
long long SystemMs(std::filesystem::file_time_type t) {
    const auto sys = std::chrono::system_clock::now() +
                     std::chrono::duration_cast<std::chrono::system_clock::duration>(t - std::filesystem::file_time_type::clock::now());
    return std::chrono::duration_cast<std::chrono::milliseconds>(sys.time_since_epoch()).count();
}

std::optional<Image> ReadPicture(const std::filesystem::path& file) {
    const Expected<std::vector<std::uint8_t>> png = doc::ReadBzcPreview(file);
    if (!png) return std::nullopt;
    Expected<Image> image = DecodeImage(*png);
    if (!image || image->width <= 0 || image->height <= 0) return std::nullopt;
    return FitWithin(*image, kSide);
}

void Upload(FilePreview& p, const Image& image) {
    if (!p.texture) glGenTextures(1, &p.texture);
    glBindTexture(GL_TEXTURE_2D, p.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    p.width = image.width;
    p.height = image.height;
}

/// The finished job into its entry.
void TakeResult() {
    if (!gJob || gJob->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    std::optional<Image> image = gJob->result.get();
    Entry& e = gEntries[gJob->key];
    e.picture.loading = false;
    if (e.stamp == gJob->stamp) {
        e.read = true;
        if (image) Upload(e.picture, *image);
        // No picture (an old file, or saved without one): an old texture
        // would show another state of the model — drop it.
        else if (e.picture.texture) {
            glDeleteTextures(1, &e.picture.texture);
            e.picture.texture = 0;
            e.picture.width = e.picture.height = 0;
        }
    }
    gJob.reset();
}

} // namespace

FilePreview FilePreviewOf(const std::filesystem::path& file) {
    const Clock::time_point now = Clock::now();
    gAsked = now;
    TakeResult();
    const std::string key = ToUtf8(file);
    Entry& e = gEntries[key];
    // The disk is asked about the file's date at most once a second.
    if (now - e.checked > std::chrono::seconds(1)) {
        e.checked = now;
        std::error_code ec;
        const auto stamp = std::filesystem::last_write_time(file, ec);
        e.picture.missing = static_cast<bool>(ec);
        if (ec) return e.picture;   // gone: the picture it had (if any) stays
        if (stamp != e.stamp) {
            e.stamp = stamp;
            e.read = false;
            e.picture.modified = SystemMs(stamp);
        }
    }
    if (!e.read && !e.picture.loading && !e.picture.missing && !gJob) {
        e.picture.loading = true;
        gJob = Job{key, e.stamp, std::async(std::launch::async, [file] { return ReadPicture(file); })};
    }
    return e.picture;
}

bool FilePreviewsBusy() {
    // Only while the home page asks: a job finished meanwhile waits for it.
    return gJob.has_value() && Clock::now() - gAsked < std::chrono::seconds(1);
}

} // namespace bz::app
