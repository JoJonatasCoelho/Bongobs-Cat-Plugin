#pragma once

#include <CubismFramework.hpp>
#include <string>


class Pal
{
public:
    static Csm::csmByte *LoadFileAsBytes(const std::string filePath,Csm::csmSizeInt *outSize);

    static void ReleaseBytes(Csm::csmByte* byteData);

    static void PrintLog(const Csm::csmChar *format, ...);

    static void PrintMessage(const Csm::csmChar* message);

    static bool IsFileExist(const Csm::csmChar *csDir);

    static int GetAllDirName(const Csm::csmChar *csDir, Csm::csmChar **Files);

    /// Returns the file name of the first "*.model3.json" found directly inside
    /// dirPath, or an empty string when the directory cannot be read or no such
    /// model file exists.
    static std::string GetModelName(const std::string &dirPath);

    /// Fills horizontal/vertical with the desktop size. On Linux this is the
    /// X11 screen size when a display is available, otherwise the OBS canvas
    /// size, otherwise 1920x1080. Callers use it only to normalize mouse
    /// coordinates, so an approximation is acceptable.
    static void GetDesktopResolution(int &horizontal, int &vertical);

    /// Returns the absolute path (with a trailing '/') of the directory that
    /// contains the "Bango Cat" resource folder, resolved through OBS's module
    /// data directory (obs_module_file). Returns an empty string when the
    /// module data directory cannot be resolved or has no "Bango Cat" folder,
    /// which preserves the legacy CWD-relative lookup used by the Windows zip
    /// layout. Concatenate with Define::ResourcesPath to get the resource path.
    static std::string GetResourceRoot();
};
