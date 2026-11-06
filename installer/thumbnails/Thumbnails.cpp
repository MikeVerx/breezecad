// BreezeCAD previews in Windows File Explorer: a shell thumbnail provider.
//
// Explorer hands the file over as a stream; the provider reads preview.png
// from the document's ZIP (written by BreezeCAD on saving, see
// docs/ASSEMBLY_FORMAT.md), scales it to the size asked and puts the
// BreezeCAD mark into the corner on large and extra large icons. A file
// without the picture gets no thumbnail and Explorer shows the type's icon.
//
// Registered per user (HKCU, no administrator rights) by DllRegisterServer,
// which the installer calls; DllUnregisterServer takes it back. Exports:
// Thumbnails.def.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <wincodec.h>

#include <miniz.h>

#include "Badge.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <new>
#include <string>
#include <vector>

using bz::thumbs::DrawBadge;

namespace {

// {6E7B4A2F-3C1D-4F8E-9B5A-0D2C7E91B4A3}
constexpr CLSID kClsid = {0x6e7b4a2f, 0x3c1d, 0x4f8e, {0x9b, 0x5a, 0x0d, 0x2c, 0x7e, 0x91, 0xb4, 0xa3}};
constexpr wchar_t kClsidText[] = L"{6E7B4A2F-3C1D-4F8E-9B5A-0D2C7E91B4A3}";
constexpr wchar_t kThumbnailHandler[] = L"{e357fccd-a995-4576-b01f-234630154e96}";   // IThumbnailProvider
constexpr const wchar_t* kExtensions[] = {L".bzc", L".bza", L".bzp"};
// Their kinds (installer/setup/Install.cpp): an empty TypeOverlay there keeps
// Explorer from putting the program's icon over the preview as well — the
// preview has its own mark (Badge.h), two of them overlapped.
constexpr const wchar_t* kProgIds[] = {L"BreezeCAD.Part", L"BreezeCAD.Assembly", L"BreezeCAD.Project"};
constexpr std::uint64_t kLargestFile = 512ull << 20;   // past that Explorer gets the plain icon

HMODULE           gModule = nullptr;
std::atomic<long> gObjects{0};
std::atomic<long> gLocks{0};

// --- reading the picture ------------------------------------------------------

bool ReadStream(IStream* stream, std::vector<std::uint8_t>& out) {
    STATSTG stat{};
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.QuadPart == 0 || stat.cbSize.QuadPart > kLargestFile) return false;
    out.resize(static_cast<std::size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER start{};
    stream->Seek(start, STREAM_SEEK_SET, nullptr);
    std::size_t done = 0;
    while (done < out.size()) {
        ULONG got = 0;
        const ULONG want = static_cast<ULONG>(std::min<std::size_t>(out.size() - done, 1u << 20));
        if (FAILED(stream->Read(out.data() + done, want, &got)) || got == 0) return false;
        done += got;
    }
    return true;
}

bool PreviewPng(const std::vector<std::uint8_t>& file, std::vector<std::uint8_t>& png) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, file.data(), file.size(), 0)) return false;
    const int index = mz_zip_reader_locate_file(&zip, "preview.png", nullptr, 0);
    bool ok = false;
    if (index >= 0) {
        std::size_t size = 0;
        if (void* data = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(index), &size, 0)) {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            png.assign(bytes, bytes + size);
            mz_free(data);
            ok = !png.empty();
        }
    }
    mz_zip_reader_end(&zip);
    return ok;
}

/// PNG -> size x size BGRA, straight alpha, through the system's decoder.
bool Decode(const std::vector<std::uint8_t>& png, UINT size, std::vector<std::uint8_t>& bgra) {
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return false;
    bool ok = false;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* converter = nullptr;
    if (SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(png.data()), static_cast<DWORD>(png.size()))) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
        SUCCEEDED(scaler->Initialize(frame, size, size, WICBitmapInterpolationModeFant)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(scaler, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                        WICBitmapPaletteTypeCustom))) {
        bgra.resize(std::size_t(size) * size * 4);
        ok = SUCCEEDED(converter->CopyPixels(nullptr, size * 4, static_cast<UINT>(bgra.size()), bgra.data()));
    }
    for (IUnknown* u : std::initializer_list<IUnknown*>{converter, scaler, frame, decoder, stream, factory})
        if (u) u->Release();
    return ok;
}

HBITMAP MakeBitmap(const std::vector<std::uint8_t>& bgra, int size) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;   // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap && bits) std::copy(bgra.begin(), bgra.end(), static_cast<std::uint8_t*>(bits));
    return bitmap;
}

// --- COM ----------------------------------------------------------------------

class Provider final : public IInitializeWithStream, public IThumbnailProvider {
public:
    Provider() { ++gObjects; }
    ~Provider() {
        if (myStream) myStream->Release();
        --gObjects;
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, __uuidof(IInitializeWithStream)))
            *out = static_cast<IInitializeWithStream*>(this);
        else if (IsEqualIID(riid, __uuidof(IThumbnailProvider)))
            *out = static_cast<IThumbnailProvider*>(this);
        else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++myRefs; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const long left = --myRefs;
        if (left == 0) delete this;
        return left;
    }

    IFACEMETHODIMP Initialize(IStream* stream, DWORD) override {
        if (myStream) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        if (!stream) return E_INVALIDARG;
        myStream = stream;
        myStream->AddRef();
        return S_OK;
    }

    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* bitmap, WTS_ALPHATYPE* alpha) override {
        if (!bitmap || !alpha) return E_POINTER;
        *bitmap = nullptr;
        if (!myStream || cx == 0) return E_UNEXPECTED;
        std::vector<std::uint8_t> file, png, bgra;
        if (!ReadStream(myStream, file) || !PreviewPng(file, png)) return E_FAIL;
        if (!Decode(png, cx, bgra)) return E_FAIL;
        if (cx >= 72) DrawBadge(bgra, static_cast<int>(cx));   // large and extra large icons
        *bitmap = MakeBitmap(bgra, static_cast<int>(cx));
        *alpha = WTSAT_ARGB;
        return *bitmap ? S_OK : E_OUTOFMEMORY;
    }

private:
    std::atomic<long> myRefs{1};
    IStream*          myStream = nullptr;
};

class Factory final : public IClassFactory {
public:
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (!IsEqualIID(riid, IID_IUnknown) && !IsEqualIID(riid, IID_IClassFactory)) {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        *out = static_cast<IClassFactory*>(this);
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return 2; }   // static
    IFACEMETHODIMP_(ULONG) Release() override { return 1; }
    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** out) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* provider = new (std::nothrow) Provider();
        if (!provider) return E_OUTOFMEMORY;
        const HRESULT hr = provider->QueryInterface(riid, out);
        provider->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL lock) override {
        lock ? ++gLocks : --gLocks;
        return S_OK;
    }
};

Factory gFactory;

// --- registration -------------------------------------------------------------

bool SetValue(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
    return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(),
                           static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

} // namespace

extern "C" {

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        gModule = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!IsEqualCLSID(clsid, kClsid)) return CLASS_E_CLASSNOTAVAILABLE;
    return gFactory.QueryInterface(riid, out);
}

HRESULT WINAPI DllCanUnloadNow() { return gObjects == 0 && gLocks == 0 ? S_OK : S_FALSE; }

HRESULT WINAPI DllRegisterServer() {
    wchar_t path[MAX_PATH * 4];
    if (!GetModuleFileNameW(gModule, path, MAX_PATH * 4)) return HRESULT_FROM_WIN32(GetLastError());
    const std::wstring clsid = std::wstring(L"Software\\Classes\\CLSID\\") + kClsidText;
    bool ok = SetValue(clsid, nullptr, L"BreezeCAD previews") && SetValue(clsid + L"\\InprocServer32", nullptr, path) &&
              SetValue(clsid + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
    for (const wchar_t* ext : kExtensions)
        ok = ok && SetValue(std::wstring(L"Software\\Classes\\") + ext + L"\\ShellEx\\" + kThumbnailHandler, nullptr, kClsidText);
    for (const wchar_t* prog : kProgIds) ok = ok && SetValue(std::wstring(L"Software\\Classes\\") + prog, L"TypeOverlay", L"");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok ? S_OK : E_FAIL;
}

HRESULT WINAPI DllUnregisterServer() {
    for (const wchar_t* ext : kExtensions)
        RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + ext + L"\\ShellEx\\" + kThumbnailHandler).c_str());
    for (const wchar_t* prog : kProgIds)
        RegDeleteKeyValueW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + prog).c_str(), L"TypeOverlay");
    RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\CLSID\\") + kClsidText).c_str());
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

} // extern "C"
