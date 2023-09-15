#include "Utilities.h"
#include "sfse_common/Log.h"
#include "sfse_common/Errors.h"
#include <string>
#include <Windows.h>

/**
 * @brief Get the path of the currently executing runtime.
 *
 * @return The path to the runtime as a string.
 */
std::string getRuntimePath()
{
    static char appPath[4096] = { 0 };

    if (appPath[0])
        return appPath;

    ASSERT(GetModuleFileName(GetModuleHandle(NULL), appPath, sizeof(appPath)));

    return appPath;
}

/**
 * @brief Get the name of the currently executing runtime.
 *
 * @return The name of the runtime as a string.
 */
std::string getRuntimeName()
{
    std::string appPath = getRuntimePath();

    std::string::size_type slashOffset = appPath.rfind('\\');
    if (slashOffset == std::string::npos)
        return appPath;

    return appPath.substr(slashOffset + 1);
}

/**
 * @brief Get the directory of the currently executing runtime.
 *
 * @return The directory of the runtime as a string.
 */
const std::string& getRuntimeDirectory()
{
    static std::string s_runtimeDirectory;

    if (s_runtimeDirectory.empty())
    {
        std::string runtimePath = getRuntimePath();

        // Truncate at the last slash.
        std::string::size_type lastSlash = runtimePath.rfind('\\');
        if (lastSlash != std::string::npos) // If we don't find a slash, something is VERY WRONG.
        {
            s_runtimeDirectory = runtimePath.substr(0, lastSlash + 1);
        }
        else
        {
            _WARNING("no slash in runtime path? (%s)", runtimePath.c_str());
        }
    }

    return s_runtimeDirectory;
}

/**
 * @brief Get the path to the configuration file.
 *
 * @return The path to the configuration file as a string.
 */
const std::string& getConfigPath()
{
    static std::string s_configPath;

    if (s_configPath.empty())
    {
        std::string runtimePath = getRuntimeDirectory();
        if (!runtimePath.empty())
        {
            s_configPath = runtimePath + "Data\\SFSE\\sfse.ini";

            _MESSAGE("config path = %s", s_configPath.c_str());
        }
    }

    return s_configPath;
}

/**
 * @brief Get a configuration option from the INI file.
 *
 * @param section The INI section.
 * @param key The INI key.
 * @return The configuration option as a string.
 */
std::string getConfigOption(const char* section, const char* key)
{
    std::string result;

    const std::string& configPath = getConfigPath();
    if (!configPath.empty())
    {
        char resultBuf[256];
        resultBuf[0] = 0;

        u32 resultLen = GetPrivateProfileString(section, key, NULL, resultBuf, sizeof(resultBuf), configPath.c_str());

        result = resultBuf;
    }

    return result;
}

/**
 * @brief Get a configuration option as an unsigned 32-bit integer from the INI file.
 *
 * @param section The INI section.
 * @param key The INI key.
 * @param dataOut Pointer to store the result.
 * @return True if successful, false otherwise.
 */
bool getConfigOption_u32(const char* section, const char* key, u32* dataOut)
{
    std::string data = getConfigOption(section, key);
    if (data.empty())
        return false;

    return (sscanf_s(data.c_str(), "%u", dataOut) == 1);
}

/**
 * @brief Get information about the operating system.
 *
 * @return A string containing information about the operating system.
 */
const std::string& getOSInfoStr()
{
    static std::string result;

    if (result.empty())
    {
        OSVERSIONINFO info;

        info.dwOSVersionInfoSize = sizeof(info);

#pragma warning (disable : 4996)
        if (GetVersionEx(&info))
        {
            char buf[256];

            sprintf_s(buf, sizeof(buf), "%d.%d (%d)", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);

            result = buf;
        }
        else
        {
            result = "unknown";
        }
    }

    return result;
}

/**
 * @brief Get the address of an import function in the Import Address Table (IAT).
 *
 * @param module The base address of the module containing the IAT.
 * @param searchDllName The name of the DLL containing the import function.
 * @param searchImportName The name of the import function.
 * @return The address of the import function or nullptr if not found.
 */
void* getIATAddr(void* module, const char* searchDllName, const char* searchImportName)
{
    u8* base = (u8*)module;
    IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* ntHeader = (IMAGE_NT_HEADERS*)(base + dosHeader->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR* importTable =
        (IMAGE_IMPORT_DESCRIPTOR*)(base + ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

    for (; importTable->Characteristics; ++importTable)
    {
        const char* dllName = (const char*)(base + importTable->Name);

        if (!_stricmp(dllName, searchDllName))
        {
            // Found the DLL.

            IMAGE_THUNK_DATA* thunkData = (IMAGE_THUNK_DATA*)(base + importTable->OriginalFirstThunk);
            uintptr_t* iat = (uintptr_t*)(base + importTable->FirstThunk);

            for (; thunkData->u1.Ordinal; ++thunkData, ++iat)
            {
                if (!IMAGE_SNAP_BY_ORDINAL(thunkData->u1.Ordinal))
                {
                    IMAGE_IMPORT_BY_NAME* importInfo = (IMAGE_IMPORT_BY_NAME*)(base + thunkData->u1.AddressOfData);

                    if (!_stricmp((char*)importInfo->Name, searchImportName))
                    {
                        // Found the import.
                        return iat;
                    }
                }
            }

            return nullptr;
        }
    }

    return nullptr;
}

/**
 * @brief Get the address of an export function in a resource library.
 *
 * @param module The base address of the resource library.
 * @param exportName The name of the export function.
 * @return The address of the export function or nullptr if not found.
 */
const void* getResourceLibraryProcAddress(const void* module, const char* exportName)
{
    auto* base = (const u8*)(uintptr_t(module) & ~3);
    auto* dosHeader = (const IMAGE_DOS_HEADER*)base;
    auto* ntHeader = (const IMAGE_NT_HEADERS*)(base + dosHeader->e_lfanew);
    auto* exportTable =
        (const IMAGE_EXPORT_DIRECTORY*)(base + ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);

    auto* exportAddresses = (const u32*)(base + exportTable->AddressOfFunctions);   // RVA array
    auto* exportNameOrdinals = (const u16*)(base + exportTable->AddressOfNameOrdinals); // index in to exportNames
    auto* exportNames = (const u32*)(base + exportTable->AddressOfNames);   // RVA array

    const void* result = nullptr;

    for (u32 i = 0; i < exportTable->NumberOfFunctions; i++)
    {
        u32 nameOrdinal = exportNameOrdinals[i];
        if (nameOrdinal < exportTable->NumberOfNames)
        {
            u32 nameRVA = exportNames[nameOrdinal];
            auto* name = (const char*)(base + nameRVA);

            if (!strcmp(exportName, name))
            {
                u32 addrRVA = exportAddresses[i];
                result = (const void*)(base + addrRVA);

                break;
            }
        }
    }

    return result;
}

/**
 * @brief Check if a module is a 64-bit DLL.
 *
 * @param module The base address of the module.
 * @return True if the module is a 64-bit DLL, false otherwise.
 */
bool is64BitDLL(const void* module)
{
    auto* base = (const u8*)(uintptr_t(module) & ~3);
    auto* dosHeader = (const IMAGE_DOS_HEADER*)base;
    auto* ntHeader = (const IMAGE_NT_HEADERS*)(base + dosHeader->e_lfanew);

    // FileHeader is PE32/64 independent
    return ntHeader->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64;
}

#pragma warning (push)
#pragma warning (disable : 4200)
struct RTTIType
{
    void* typeInfo;
    u64 data;
    char name[0];
};

struct RTTILocator
{
    u32 sig, offset, cdOffset;
    u32 typeDesc;
    u32 classDesc;
};
#pragma warning (pop)

/**
 * @brief Get the class name of an object using RTTI information.
 *
 * @param objBase The base address of the object.
 * @return The class name as a string.
 */
const char* getObjectClassName(void* objBase)
{
    const char* result = "<no rtti>";
    __try
    {
        void** obj = (void**)objBase;
        RTTILocator** vtbl = (RTTILocator**)obj[0];
        RTTILocator* rtti = vtbl[-1];
        u64 typeDesc = rtti->typeDesc;
        RelocPtr<RTTIType> type(typeDesc);

        // Starts with ,?
        if ((type->name[0] == '.') && (type->name[1] == '?'))
        {
            // Is at most 100 chars long
            for (u32 i = 0; i < 100; i++)
            {
                if (type->name[i] == 0)
                {
                    // Remove the .?AV
                    result = type->name + 4;
                    break;
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // Return the default
    }

    return result;
}
