#pragma once
// What the installer does to the computer (Windows only): checks the folder,
// unpacks, registers the program (Apps & features, file types, previews),
// makes shortcuts and takes all of it back on removal. No interface here:
// every step reports through its return value and a progress callback.
//
// Everything goes to the current user (HKCU, %APPDATA%, the user's Start
// menu), so no administrator rights are needed unless the chosen folder
// itself asks for them (Program Files): then only the copying runs elevated.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bz::setup {

inline constexpr const wchar_t* kProductKey = L"BreezeCAD";   // Uninstall\<key>, Software\<key>

/// What the person chose on the installer's pages.
struct Choices {
    std::filesystem::path    dir;
    std::string              language = "ru";   ///< the program's interface language
    std::vector<std::string> modules;           ///< ids of the official modules to put in
    bool desktop = true, startMenu = true;
    bool openParts = true, openAssemblies = true, openModules = true, openLibraries = false;
};

/// A BreezeCAD put on this computer by the installer before.
struct Installed {
    std::filesystem::path dir;
    std::string           version;
};
std::optional<Installed> FindInstalled();

/// %LOCALAPPDATA%\Programs\BreezeCAD: the user's own, no administrator needed.
std::filesystem::path DefaultDir();
/// The program's settings, modules and languages (they outlive updates).
std::filesystem::path ConfigDir();

enum class PathState {
    Ok,           ///< free to use
    NeedsAdmin,   ///< fine, but writing there takes administrator rights
    Empty,
    NotAbsolute,  ///< no drive letter (or \\server\share)
    DriveRoot,
    BadCharacters,
    NoDrive,      ///< the drive letter is not there
    SystemFolder, ///< inside Windows
    Foreign,      ///< a folder with other files in it
    NoSpace,
};
struct PathCheck {
    PathState     state = PathState::Empty;
    std::uint64_t freeBytes = 0;
    wchar_t       drive = 0;
};
/// `needBytes`: what the chosen parts take unpacked.
PathCheck CheckPath(const std::wstring& text, std::uint64_t needBytes);

bool IsElevated();
/// Runs this installer again elevated with `arguments`; waits for it and
/// returns its exit code (nullopt: the person said no to the prompt).
std::optional<unsigned long> RunElevated(const std::wstring& arguments);

/// Whether breezecad.exe from `dir` is running (it holds its files).
bool ProgramRunning(const std::filesystem::path& dir);

using Progress = std::function<void(std::uint64_t done, std::uint64_t total)>;

/// Unpacks a ZIP held in memory into `dir`; `stripTop`: the archive's single
/// top folder ("BreezeCAD/") is left out. A file that is in use (the previews
/// library Explorer keeps loaded) is renamed out of the way first.
/// `written` gets the files' paths relative to `dir` ("/" separated).
bool Unzip(std::span<const std::uint8_t> zip, const std::filesystem::path& dir, bool stripTop, const Progress& progress,
           std::vector<std::string>* written, std::wstring& error);

/// Puts a module package where the program installs it from on its next start.
bool PlaceModule(std::span<const std::uint8_t> package, const std::string& fileName, std::wstring& error);
/// Unpacks a language pack into the program's language folder.
bool PlaceLanguage(std::span<const std::uint8_t> pack, std::wstring& error);
/// Writes the interface language into the program's settings (keeps the rest).
void SetProgramLanguage(const std::string& code);

/// Localised names of the four file types, for Explorer's "Type" column.
struct TypeNames {
    std::wstring part, assembly, module, library;
};

/// In the program's folder (elevated when the folder needs it): the marker
/// the program recognises an installed copy by, and `installerExe` (this
/// installer without its packed files) as uninstall.exe, which also does
/// "Change" (a newer version over this one).
bool FinishFolder(const std::filesystem::path& dir, const std::string& version, std::span<const std::uint8_t> installerExe,
                  std::wstring& error);
/// For the person (HKCU): Apps & features entry, file types, previews.
bool Register(const Choices& c, const std::string& version, std::uint64_t sizeBytes, const TypeNames& names, std::wstring& error);
/// The list of the program's files from the last unpacking: what an update
/// removes when the new version no longer has it, and what removal deletes.
void ForgetStaleFiles(const std::filesystem::path& dir, const std::vector<std::string>& now);
bool MakeShortcuts(const Choices& c, std::wstring& error);

/// Takes everything back. `keepSettings`: the settings, modules and
/// languages in the profile stay (for a later install).
bool Uninstall(const std::filesystem::path& dir, bool keepSettings, std::wstring& error);

/// The folder of the running installer and its own file.
std::filesystem::path SelfPath();

/// Starts the installed program (not elevated, as the person).
void Launch(const std::filesystem::path& dir);
void OpenUrl(const std::wstring& url);

/// The folder picker; nullopt when cancelled.
std::optional<std::filesystem::path> PickFolder(void* owner, const std::filesystem::path& start, const std::wstring& title);

/// "UTF-8 <-> UTF-16".
std::wstring Wide(const std::string& utf8);
std::string  Utf8(const std::wstring& wide);

} // namespace bz::setup
