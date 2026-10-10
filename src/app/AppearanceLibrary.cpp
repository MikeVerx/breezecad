#include "app/AppearanceLibrary.h"

#include "core/Log.h"
#include "core/PathText.h"

#include <miniz.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <fstream>
#include <iterator>
#include <map>

namespace bz::app {
namespace {

using Json = nlohmann::json;

constexpr const char* kPackEntry = "appearances.json";

std::string ReadText(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// A file name that stays valid for any id.
std::filesystem::path FileFor(const std::filesystem::path& folder, std::string_view id) {
    std::string name;
    for (const char c : id) name += (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-') ? c : '_';
    return folder / (name + ".json");
}

std::vector<doc::Appearance> FromJsonText(const std::string& text) {
    std::vector<doc::Appearance> out;
    const Json j = Json::parse(text, nullptr, false);
    if (j.is_discarded()) return out;
    const auto take = [&](const Json& item) {
        if (auto a = doc::AppearanceFromJson(item)) out.push_back(std::move(*a));
    };
    if (j.is_array()) for (const Json& item : j) take(item);
    else take(j);
    return out;
}

} // namespace

void AppearanceLibrary::Reload() {
    myItems.clear();
    std::error_code ec;
    if (!std::filesystem::is_directory(myFolder, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(myFolder, ec)) {
        if (entry.path().extension() != ".json") continue;
        for (auto& a : FromJsonText(ReadText(entry.path())))
            if (!Find(a.id)) myItems.push_back(std::move(a));
    }
    std::ranges::sort(myItems, {}, &doc::Appearance::name);
}

const doc::Appearance* AppearanceLibrary::Find(std::string_view id) const {
    const auto it = std::ranges::find(myItems, id, &doc::Appearance::id);
    return it != myItems.end() ? &*it : nullptr;
}

std::vector<std::uint8_t> AppearanceLibrary::PictureBytes(std::string_view picture) const {
    if (picture.empty()) return {};
    const std::string text = ReadText(myFolder / std::string(picture));
    return {text.begin(), text.end()};
}

Expected<doc::Appearance> AppearanceLibrary::Save(doc::Appearance appearance, const std::filesystem::path& pictureFile) {
    if (appearance.id.empty() || appearance.id.starts_with("std.")) {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        appearance.id = std::format("user.{}", std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }
    std::error_code ec;
    std::filesystem::create_directories(myFolder, ec);
    if (!pictureFile.empty()) {
        std::string ext = ToUtf8(pictureFile.extension());
        std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::string name = FileFor(myFolder, appearance.id).stem().string() + ext;
        std::filesystem::copy_file(pictureFile, myFolder / name, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) return Error{"Не удалось скопировать картинку в библиотеку"};
        appearance.picture = name;
    }
    std::ofstream out(FileFor(myFolder, appearance.id), std::ios::binary | std::ios::trunc);
    out << doc::ToJson(appearance).dump(1, '\t');
    if (!out) return Error{"Не удалось сохранить внешний вид в папку настроек"};
    if (auto it = std::ranges::find(myItems, appearance.id, &doc::Appearance::id); it != myItems.end()) *it = appearance;
    else myItems.push_back(appearance);
    return appearance;
}

Status AppearanceLibrary::Remove(std::string_view id) {
    std::error_code ec;
    if (const doc::Appearance* a = Find(id); a && a->HasPicture()) std::filesystem::remove(myFolder / a->picture, ec);
    std::filesystem::remove(FileFor(myFolder, id), ec);
    std::erase_if(myItems, [&](const doc::Appearance& a) { return a.id == id; });
    if (ec) return Error{"Не удалось удалить файл внешнего вида"};
    return Status::Ok();
}

Expected<int> AppearanceLibrary::Import(const std::filesystem::path& file) {
    std::vector<doc::Appearance> found;
    std::map<std::string, std::string> pictures;   // name in the pack -> bytes
    if (file.extension() == ".json") {
        found = FromJsonText(ReadText(file));
    } else {
        const std::string bytes = ReadText(file);
        mz_zip_archive zip{};
        if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) return Error{"Файл не похож на ZIP-архив"};
        for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
            char name[512];
            mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
            const std::string entry = name;
            std::size_t size = 0;
            void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
            if (!data) continue;
            const std::string bytes(static_cast<const char*>(data), size);
            mz_free(data);
            if (entry.ends_with(".json")) {
                for (auto& a : FromJsonText(bytes)) found.push_back(std::move(a));
            } else if (entry.starts_with("pictures/")) {
                pictures[entry.substr(9)] = bytes;
            }
        }
        mz_zip_reader_end(&zip);
    }
    if (found.empty()) return Error{"В файле нет внешних видов BreezeCAD"};
    int added = 0;
    for (doc::Appearance& a : found) {
        if (a.id.starts_with("std.")) a.id.clear();   // never shadows a standard one
        if (a.HasPicture()) {
            const auto picture = pictures.find(a.picture);
            if (picture == pictures.end()) continue;   // a picture appearance without its picture
            // Through a file next to the library, then copied in under the new id.
            const std::filesystem::path temp = myFolder / ("import-" + a.picture);
            std::error_code ec;
            std::filesystem::create_directories(myFolder, ec);
            std::ofstream(temp, std::ios::binary) << picture->second;
            const bool saved = static_cast<bool>(Save(std::move(a), temp));
            std::filesystem::remove(temp, ec);
            if (saved) ++added;
            continue;
        }
        if (Save(std::move(a))) ++added;
    }
    return added;
}

Status AppearanceLibrary::Export(const std::vector<doc::Appearance>& items, const std::filesystem::path& file) const {
    Json all = Json::array();
    for (const auto& a : items) all.push_back(doc::ToJson(a));
    const std::string text = all.dump(1, '\t');
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 16 * 1024)) return Error{"Не удалось подготовить архив"};
    void* buffer = nullptr;
    std::size_t size = 0;
    bool ok = mz_zip_writer_add_mem(&zip, kPackEntry, text.data(), text.size(), MZ_BEST_COMPRESSION);
    for (const auto& a : items) {
        if (!a.HasPicture()) continue;
        const auto bytes = PictureBytes(a.picture);
        ok = ok && mz_zip_writer_add_mem(&zip, ("pictures/" + a.picture).c_str(), bytes.data(), bytes.size(), MZ_NO_COMPRESSION);
    }
    ok = ok && mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
    mz_zip_writer_end(&zip);
    if (!ok) return Error{"Не удалось собрать архив"};
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(size));
    mz_free(buffer);
    if (!out) return Error{"Не удалось записать файл"};
    return Status::Ok();
}

} // namespace bz::app
