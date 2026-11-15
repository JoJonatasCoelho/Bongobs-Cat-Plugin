#include "Pal.hpp"
#include <iostream>
#include <fstream>
#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <filesystem>
#include <Model/CubismMoc.hpp>
#include "Define.hpp"
#include <obs-module.h>

#ifdef _WIN32
#include <Windows.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#endif

using std::endl;
using namespace Csm;
using namespace std;
using namespace Define;

namespace {
// std::filesystem returns UTF-8 strings; in C++20 .u8string() is std::u8string
// instead of std::string, so convert explicitly to keep both standards building.
std::string PathToUtf8(const std::filesystem::path &path)
{
#if defined(__cpp_char8_t)
    const std::u8string utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
#else
    return path.u8string();
#endif
}
}

csmByte* Pal::LoadFileAsBytes(const string filePath, csmSizeInt* outSize)
{
    if (outSize != NULL)
    {
        *outSize = 0;
    }

    // Resolve the size first so the allocation matches the actual file length.
    std::error_code ec;
    // u8path keeps UTF-8 paths working on Windows as well (converts to wide
    // internally) while behaving like a plain path on Linux.
    const std::filesystem::path path = std::filesystem::u8path(filePath);
    const std::uintmax_t fileSize = std::filesystem::file_size(path, ec);
    if (ec)
    {
        return NULL;
    }

#if defined(_WIN32)
    // MSVC: path::value_type is wchar_t and ifstream has a wchar_t overload,
    // so the wide path is used (keeps UTF-8 paths working on Windows).
    std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
#else
    std::ifstream file(path, std::ios::in | std::ios::binary);
#endif
    if (!file.is_open())
    {
        return NULL;
    }

    csmByte* buf = new csmByte[static_cast<size_t>(fileSize)];
    if (fileSize > 0)
    {
        file.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(fileSize));
        if (!file)
        {
            file.close();
            delete[] buf;
            return NULL;
        }
    }
    file.close();

    if (outSize != NULL)
    {
        *outSize = static_cast<csmSizeInt>(fileSize);
    }
    return buf;
}

void Pal::ReleaseBytes(csmByte* byteData)
{
    delete[] byteData;
}

void Pal::PrintLog(const csmChar *format, ...)
{
    va_list args;
    csmChar buf[256];
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args); // 標準出力でレンダリング
    va_end(args);
#ifdef CSM_DEBUG_MEMORY_LEAKING
// メモリリークチェック時は大量の標準出力がはしり重いのでprintfを利用する
    std::printf("%s", buf);
#else
    std::cerr << buf << std::endl;
#endif
}

void Pal::PrintMessage(const csmChar* message)
{
    PrintLog("%s", message);
}

bool Pal::IsFileExist(const Csm::csmChar *csDir)
{
    if (csDir == NULL)
    {
        return false;
    }

    std::error_code ec;
    return std::filesystem::exists(std::filesystem::u8path(csDir), ec);
}

int Pal::GetAllDirName(const Csm::csmChar *csDir, Csm::csmChar **Files)
{
    return 0;
}

std::string Pal::GetModelName(const std::string &dirPath)
{
    static const std::string suffix = ".model3.json";
    std::error_code ec;

    const std::filesystem::path directory = std::filesystem::u8path(dirPath);

    for (std::filesystem::directory_iterator it(directory, ec), end;
         !ec && it != end;
         it.increment(ec))
    {
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc) || entryEc)
        {
            continue;
        }

        const std::string name = PathToUtf8(it->path().filename());
        if (name.size() >= suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
        {
            return name;
        }
    }

    return std::string();
}

void Pal::GetDesktopResolution(int &horizontal, int &vertical) {
#ifdef _WIN32
    RECT desktop;
    const HWND hDesktop = GetDesktopWindow();
    GetWindowRect(hDesktop, &desktop);
    horizontal = desktop.right;
    vertical = desktop.bottom;
#elif defined(__linux__)
    // Live2DManager calls this every frame only to normalize mouse coordinates.
    // Prefer the X11 screen size when a display is reachable; otherwise fall
    // back to the OBS canvas size, then to 1920x1080. The X server is queried at
    // most once per second so we do not pay a round-trip on every frame.
    static int cachedWidth = 0;
    static int cachedHeight = 0;
    static bool hasQueried = false;
    static std::chrono::steady_clock::time_point lastQuery;

    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (!hasQueried || now - lastQuery >= std::chrono::seconds(1))
    {
        hasQueried = true;
        lastQuery = now;

        Display *display = XOpenDisplay(NULL);
        if (display != NULL)
        {
            const int screen = DefaultScreen(display);
            cachedWidth = XDisplayWidth(display, screen);
            cachedHeight = XDisplayHeight(display, screen);
            XCloseDisplay(display);
        }
    }

    if (cachedWidth > 0 && cachedHeight > 0)
    {
        horizontal = cachedWidth;
        vertical = cachedHeight;
        return;
    }

    struct obs_video_info ovi;
    if (obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0)
    {
        horizontal = static_cast<int>(ovi.base_width);
        vertical = static_cast<int>(ovi.base_height);
        return;
    }

    horizontal = 1920;
    vertical = 1080;
#else
    // Unknown platform: use a safe default so mouse normalization stays valid.
    horizontal = 1920;
    vertical = 1080;
#endif
}

std::string Pal::GetResourceRoot()
{
    std::error_code ec;

    // Normalize to an absolute directory prefix ending with exactly one '/'.
    auto withTrailingSlash = [](const std::filesystem::path &path) {
        if (path.empty())
        {
            return std::string();
        }
        std::string result = PathToUtf8(path);
        if (result.empty() || result.back() != '/')
        {
            result.push_back('/');
        }
        return result;
    };

    // Preferred layout: OBS installs the resources into the module data dir, so
    // "<data>/Bango Cat" exists. obs_module_file returns a bfree-allocated
    // string (or NULL).
    char *bangoDir = obs_module_file("Bango Cat");
    if (bangoDir != NULL)
    {
        const std::filesystem::path candidate = std::filesystem::u8path(bangoDir);
        bfree(bangoDir);
        if (std::filesystem::is_directory(candidate, ec))
        {
            // GetResourceRoot is the parent; Define::ResourcesPath adds
            // "Bango Cat/" on top of it.
            return withTrailingSlash(candidate.parent_path());
        }
    }

    // Same idea using the module data directory itself, in case the direct
    // lookup above failed.
    ec.clear();
    char *dataDir = obs_module_file("");
    if (dataDir != NULL)
    {
        const std::filesystem::path candidate =
            std::filesystem::u8path(dataDir) / "Bango Cat";
        bfree(dataDir);
        if (std::filesystem::is_directory(candidate, ec))
        {
            return withTrailingSlash(candidate.parent_path());
        }
    }

    // Legacy fallback: let callers keep resolving paths relative to the current
    // working directory, matching the old Windows zip layout.
    return std::string();
}
