#include <Windows.h>
#include <ShlObj.h>
#include <corecrt_startup.h>
#include "sfse_common/Log.h"
#include "sfse_common/sfse_version.h"
#include "sfse_common/Utilities.h"
#include "sfse_common/SafeWrite.h"
#include "sfse_common/BranchTrampoline.h"
#include "PluginManager.h"

#include "Hooks_Version.h"
#include "Hooks_Script.h"

// Global variable to store the module handle.
HINSTANCE g_moduleHandle = nullptr;

// Function prototypes
void SFSE_Preinit();
void SFSE_Initialize();

/**
 * @brief The entry point for initializing additional scripting capabilities and functionality.
 *
 * This function runs before global initializers and can be used for optional plugin preload.
 *
 * @param a Pointer to an array of function pointers.
 * @param b Pointer to another array of function pointers.
 * @return The result of the original initialization function.
 */
int __initterm_e_Hook(_PIFV* a, _PIFV* b)
{
    SFSE_Preinit();
    return _initterm_e_Original(a, b);
}

/**
 * @brief Hook for retrieving the narrow WinMain command line.
 *
 * This function runs after global initializers and performs the usual load-time tasks.
 *
 * @return The narrow WinMain command line.
 */
char* __get_narrow_winmain_command_line_Hook()
{
    SFSE_Initialize();
    return _get_narrow_winmain_command_line_Original();
}

/**
 * @brief Install base hooks for the game.
 */
void installBaseHooks(void)
{
    // Open a debug log file.
    DebugLog::openRelative(CSIDL_MYDOCUMENTS, "\\My Games\\" SAVE_FOLDER_NAME "\\SFSE\\Logs\\sfse.txt");

    // Get the module handle for the executable.
    HANDLE exe = GetModuleHandle(nullptr);

    // Fetch functions to hook.
    auto* initterm = (__initterm_e*)getIATAddr(exe, "api-ms-win-crt-runtime-l1-1-0.dll", "_initterm_e");
    auto* cmdline = (__get_narrow_winmain_command_line*)getIATAddr(exe, "api-ms-win-crt-runtime-l1-1-0.dll", "_get_narrow_winmain_command_line");

    // Hook the functions.
    if (initterm)
    {
        _initterm_e_Original = *initterm;
        safeWrite64(uintptr_t(initterm), u64(__initterm_e_Hook));
    }
    else
    {
        _ERROR("couldn't find _initterm_e");
    }

    if (cmdline)
    {
        _get_narrow_winmain_command_line_Original = *cmdline;
        safeWrite64(uintptr_t(cmdline), u64(__get_narrow_winmain_command_line_Hook));
    }
    else
    {
        _ERROR("couldn't find _get_narrow_winmain_command_line");
    }
}

/**
 * @brief Wait for a debugger to be attached.
 */
void WaitForDebugger(void)
{
    while (!IsDebuggerPresent())
    {
        Sleep(10);
    }

    Sleep(1000 * 2);
}

/**
 * @brief Perform pre-initialization tasks for SFSE.
 */
void SFSE_Preinit()
{
    static bool runOnce = false;
    if (runOnce)
        return;
    runOnce = true;

    SYSTEMTIME now;
    GetSystemTime(&now);

    _MESSAGE("SFSE runtime: initialize (version = %d.%d.%d %08X %04d-%02d-%02d %02d:%02d:%02d, os = %s)",
        SFSE_VERSION_INTEGER, SFSE_VERSION_INTEGER_MINOR, SFSE_VERSION_INTEGER_BETA, RUNTIME_VERSION,
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        getOSInfoStr().c_str());

    _MESSAGE("imagebase = %016I64X", g_moduleHandle);
    _MESSAGE("reloc mgr imagebase = %016I64X", RelocationManager::s_baseAddr);

#ifdef _DEBUG
    SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);

    WaitForDebugger();
#endif

    if (!g_branchTrampoline.create(1024 * 64))
    {
        _ERROR("couldn't create branch trampoline. this is fatal. skipping remainder of init process.");
        return;
    }

    if (!g_localTrampoline.create(1024 * 64, g_moduleHandle))
    {
        _ERROR("couldn't create codegen buffer. this is fatal. skipping remainder of init process.");
        return;
    }

    // Scan the plugin folder.
    g_pluginManager.init();

    // Preload plugins.
    g_pluginManager.installPlugins(PluginManager::kPhase_Preload);

    _MESSAGE("preinit complete");
}

/**
 * @brief Perform initialization tasks for SFSE.
 */
void SFSE_Initialize()
{
    static bool runOnce = false;
    if (runOnce)
        return;
    runOnce = true;

    // Load plugins.
    g_pluginManager.installPlugins(PluginManager::kPhase_Load);
    g_pluginManager.loadComplete();

    Hooks_Version_Apply();
    Hooks_Script_Apply();

    FlushInstructionCache(GetCurrentProcess(), NULL, 0);

    _MESSAGE("init complete");

    DebugLog::flush();
}

/**
 * @brief Entry point for starting SFSE.
 */
extern "C" {
    void StartSFSE()
    {
        installBaseHooks();
    }

    /**
     * @brief DllMain function for the SFSE DLL.
     *
     * @param hDllHandle The handle to the DLL.
     * @param dwReason The reason for calling this function.
     * @param lpreserved Reserved parameter.
     * @return TRUE if successful, FALSE otherwise.
     */
    BOOL WINAPI DllMain(HANDLE hDllHandle, DWORD dwReason, LPVOID lpreserved)
    {
        switch (dwReason)
        {
        case DLL_PROCESS_ATTACH:
            g_moduleHandle = (HINSTANCE)hDllHandle;
            break;

        case DLL_PROCESS_DETACH:
            break;
        };

        return TRUE;
    }
}
