// Copyright 2026 Autodesk, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Verify usdview startup with the Aurora render delegate.
#if (defined(__APPLE__) || defined(_WIN32)) && defined(NDEBUG)
#ifndef DISABLE_UNIT_TESTS

#include <gtest/gtest.h>

#include "TestHelpers.h"

#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/xform.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <spawn.h>
#include <sys/wait.h>
#endif

PXR_NAMESPACE_USING_DIRECTIVE

#if !defined(_WIN32)
extern char** environ;
#endif

namespace
{
// Paths injected by CMake.
constexpr const char* kUsdRoot       = TOSTRING(AURORA_USD_ROOT);
constexpr const char* kUsdPythonDir  = TOSTRING(AURORA_USD_PYTHON_DIR);
constexpr const char* kBuildLibDir   = TOSTRING(AURORA_BUILD_LIB_DIR);
constexpr const char* kBuildBinDir   = TOSTRING(AURORA_BUILD_BIN_DIR);
constexpr const char* kGlewLibDir    = TOSTRING(AURORA_GLEW_LIB_DIR);
constexpr const char* kSlangLibDir   = TOSTRING(AURORA_SLANG_LIB_DIR);

// The search path the loader uses for shared libraries, and its separator.
#if defined(_WIN32)
constexpr const char* kLibraryPathVar = "PATH";
constexpr char kPathSeparator         = ';';
#else
constexpr const char* kLibraryPathVar = "DYLD_LIBRARY_PATH";
constexpr char kPathSeparator         = ':';
#endif

// Write a minimal scene for usdview startup.
bool WriteTestScene(const std::string& path)
{
    UsdStageRefPtr stage = UsdStage::CreateNew(path);
    if (!stage)
        return false;

    UsdGeomXform root = UsdGeomXform::Define(stage, SdfPath("/World"));
    UsdGeomCube cube = UsdGeomCube::Define(stage, SdfPath("/World/Cube"));
    cube.GetSizeAttr().Set(2.0);
    stage->SetDefaultPrim(root.GetPrim());
    stage->Save();
    return true;
}

std::string JoinPaths(const std::vector<std::string>& paths)
{
    std::string result;
    for (const auto& p : paths)
    {
        if (p.empty())
            continue;
        if (!result.empty())
            result += kPathSeparator;
        result += p;
    }
    return result;
}

std::string GetEnvVar(const char* name)
{
#if defined(_WIN32)
    // getenv() is deprecated by MSVC.
    const DWORD length = ::GetEnvironmentVariableA(name, nullptr, 0);
    if (length == 0)
        return {};
    std::string value(length, '\0');
    const DWORD written = ::GetEnvironmentVariableA(name, value.data(), length);
    value.resize(written);
    return value;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

void SetEnvVar(const char* name, const std::string& value)
{
#if defined(_WIN32)
    ::SetEnvironmentVariableA(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

#if defined(_WIN32)
// Windows can't execute Python scripts directly; extract the interpreter from the shebang.
std::string ReadShebangInterpreter(const std::string& scriptPath)
{
    std::ifstream script(scriptPath);
    std::string line;
    if (!std::getline(script, line))
        return {};
    if (line.rfind("#!", 0) != 0)
        return {};

    const std::string interpreter = line.substr(2);
    const size_t first            = interpreter.find_first_not_of(" \t");
    const size_t last             = interpreter.find_last_not_of(" \t\r");
    if (first == std::string::npos)
        return {};
    return interpreter.substr(first, last - first + 1);
}

// Quote one command line argument following the CommandLineToArgvW rules.
std::string QuoteArgument(const std::string& arg)
{
    if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos)
        return arg;

    std::string quoted = "\"";
    for (size_t i = 0; i < arg.size(); ++i)
    {
        size_t backslashCount = 0;
        while (i < arg.size() && arg[i] == '\\')
        {
            ++backslashCount;
            ++i;
        }
        if (i == arg.size())
        {
            // Trailing backslashes precede the closing quote, so they must be doubled.
            quoted.append(backslashCount * 2, '\\');
            break;
        }
        // A backslash run is only special when it precedes a quote.
        quoted.append(arg[i] == '"' ? backslashCount * 2 + 1 : backslashCount, '\\');
        quoted += arg[i];
    }
    quoted += '"';
    return quoted;
}

// Run a child process to completion and return its exit code.
int RunProcess(const std::vector<std::string>& args, const std::string& workingDir)
{
    std::string commandLine;
    for (const auto& arg : args)
    {
        if (!commandLine.empty())
            commandLine += ' ';
        commandLine += QuoteArgument(arg);
    }

    STARTUPINFOA startupInfo {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo {};

    // CreateProcess may modify the command line buffer, so it must be writable.
    std::vector<char> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back('\0');

    if (!::CreateProcessA(args[0].c_str(), mutableCommandLine.data(), nullptr, nullptr, FALSE, 0,
            nullptr, workingDir.empty() ? nullptr : workingDir.c_str(), &startupInfo, &processInfo))
    {
        return -static_cast<int>(::GetLastError());
    }

    ::WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode         = 0;
    const BOOL gotExitCode = ::GetExitCodeProcess(processInfo.hProcess, &exitCode);
    ::CloseHandle(processInfo.hThread);
    ::CloseHandle(processInfo.hProcess);

    return gotExitCode ? static_cast<int>(exitCode) : -1;
}
#else
int RunProcess(const std::vector<std::string>& args, const std::string& /* workingDir */)
{
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args)
    {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid          = 0;
    const int spawnErr = posix_spawn(&pid, args[0].c_str(), nullptr, nullptr, argv.data(), environ);
    if (spawnErr != 0)
    {
        return -spawnErr;
    }

    int status = 0;
    while (waitpid(pid, &status, 0) == -1)
    {
        if (errno != EINTR)
        {
            return -errno;
        }
    }

    if (WIFEXITED(status))
    {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status))
    {
        return 128 + WTERMSIG(status);
    }
    return -1;
}
#endif

// The usdview entry point to launch, or an empty string if this externals tree has no usdview.
std::string FindUsdView()
{
#if defined(_WIN32)
    const std::string deployed = std::string(kBuildBinDir) + "/usdview";
    if (TestHelpers::fileIsReadable(deployed))
        return deployed;
#endif
    const std::string installed = std::string(kUsdRoot) + "/bin/usdview";
    return TestHelpers::fileIsReadable(installed) ? installed : std::string();
}

// Launch usdview with the Aurora runtime environment.
int RunUsdView(const std::string& usdviewScript, const std::string& scenePath)
{
    const std::string libraryPath = JoinPaths({ kBuildLibDir, kBuildBinDir, kGlewLibDir,
        kSlangLibDir, std::string(kUsdRoot) + "/lib", std::string(kUsdRoot) + "/bin",
        GetEnvVar(kLibraryPathVar) });

    SetEnvVar("PYTHONPATH", kUsdPythonDir);
    SetEnvVar(kLibraryPathVar, libraryPath);
    SetEnvVar("HD_DEFAULT_RENDERER", "Aurora");

    std::vector<std::string> args;
#if defined(_WIN32)
    // Windows locates plugins relative to the USD DLLs. Ignore PXR_PLUGINPATH_NAME to avoid
    // registering hdAurora twice.
    const std::string interpreter = ReadShebangInterpreter(usdviewScript);
    if (interpreter.empty() || !TestHelpers::fileIsReadable(interpreter))
    {
        ADD_FAILURE() << "Could not resolve a Python interpreter from the shebang of "
                      << usdviewScript << " (read \"" << interpreter << "\").";
        return -1;
    }
    args.push_back(interpreter);
#else
    SetEnvVar("PXR_PLUGINPATH_NAME", std::string(kBuildBinDir) + "/usd");
#endif
    args.push_back(usdviewScript);
    args.push_back("--renderer=Aurora");
#if !defined(_WIN32)
    args.push_back("--norender");
#endif
    args.push_back("--quitAfterStartup");
    args.push_back("--defaultsettings");
    args.push_back(scenePath);

    return RunProcess(args, kBuildBinDir);
}

class UsdViewTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Write the test scene into the source assets directory.
        const std::string dir =
            TestHelpers::kSourceRoot + "/Tests/Assets/Geometry";
        if (!TestHelpers::directoryExists(dir))
        {
            TestHelpers::createDirectory(dir);
        }
        _scenePath = TestHelpers::combinePaths(dir, "usdview_test_cube.usda");
        ASSERT_TRUE(WriteTestScene(_scenePath))
            << "Failed to write test USD scene to " << _scenePath;
    }

    void TearDown() override
    {
        std::remove(_scenePath.c_str());
    }

    std::string _scenePath;
};

// Verify usdview launches and exits cleanly with the Aurora renderer.
TEST_F(UsdViewTest, LaunchesWithAuroraRenderer)
{
    const std::string usdviewScript = FindUsdView();

    // Skip if usdview is not installed in this externals tree.
    if (usdviewScript.empty())
    {
        GTEST_SKIP() << "usdview not found under " << kUsdRoot
                     << "; skipping usdview launch test.";
    }

    const int exitCode = RunUsdView(usdviewScript, _scenePath);
    EXPECT_EQ(exitCode, 0)
        << "usdview did not exit cleanly (exit code " << exitCode
        << "). See stdout/stderr above for details.";
}

} // namespace

#endif // DISABLE_UNIT_TESTS
#else
// Keep the test target buildable when this test is disabled.
int g_usdview_test_placeholder = 0;
#endif
