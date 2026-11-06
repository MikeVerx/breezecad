#pragma once
// The pictures saved inside BreezeCAD files (preview.png, written on saving:
// see document/BzcFile.h) as textures for the home page's recent files.
//
// A file is read in the background, one at a time, so a large assembly
// never holds the frame; until it is ready (or when the file has no
// picture) the texture is 0 and the caller draws the kind's icon. The
// picture is read again when the file changes on disk.

#include <filesystem>

namespace bz::app {

struct FilePreview {
    unsigned texture = 0;        ///< GL texture, 0: none (yet)
    int      width = 0, height = 0;
    bool     loading = false;    ///< being read now
    bool     missing = false;    ///< no such file (as of the last look, at most a second ago)
    long long modified = 0;      ///< when the file was last written, ms since 1970 (0: unknown)
};

/// The picture of `file` (0 texture while it loads or when there is none).
/// Call from the frame (the GL thread).
FilePreview FilePreviewOf(const std::filesystem::path& file);

/// Pictures were read in the background since the last frame: draw another.
bool FilePreviewsBusy();

} // namespace bz::app
