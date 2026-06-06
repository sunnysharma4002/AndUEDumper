#include "Dumper.hpp"

#include <fmt/format.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "UE/UEMemory.hpp"
using namespace UEMemory;

#include "UPackageGenerator.hpp"

#define kVECTOR_CONTAINS(vec, val) (std::find(vec.begin(), vec.end(), val) != vec.end())

namespace dumper_jf_ns
{
    static uintptr_t base_address = 0;
    struct JsonFunction
    {
        std::string Parent;
        std::string Name;
        uint64_t Address = 0;
    };
    static std::vector<JsonFunction> jsonFunctions;

    void to_json(json &j, const JsonFunction &jf)
    {
        if (jf.Parent.empty() || jf.Parent == "None" || jf.Parent == "null")
            return;
        if (jf.Name.empty() || jf.Name == "None" || jf.Name == "null")
            return;
        if (jf.Address == 0 || jf.Address <= base_address)
            return;

        std::string fname = IOUtils::replace_specials(jf.Parent, '_');
        fname += "$$";
        fname += IOUtils::replace_specials(jf.Name, '_');

        j = json{{"Name", fname}, {"Address", (jf.Address - base_address)}};
    }
}  // namespace dumper_jf_ns

namespace
{
    bool StartsWith(const std::string &s, const char *prefix)
    {
        return s.rfind(prefix, 0) == 0;
    }

    bool IsDumpableObjectByName(const UE_UObject &object)
    {
        std::string fullName = object.GetFullName();
        return StartsWith(fullName, "Class ") ||
               StartsWith(fullName, "ScriptStruct ") ||
               StartsWith(fullName, "Enum ") ||
               StartsWith(fullName, "Function ");
    }

    void AppendSDKPreamble(BufferFmt &buffer)
    {
        buffer.append("#pragma once\n\n#include <cstdio>\n#include <string>\n#include <cstdint>\n\n\n");
    }

    std::string MakeSDKHeaderPath(const std::string &packageName)
    {
        std::string cleanName = IOUtils::replace_specials(packageName, '_');
        if (cleanName.empty())
            cleanName = "UnknownPackage";

        return "SDK/" + cleanName + ".hpp";
    }

    void AppendPackageSDK(BufferFmt &buffer, const UE_UPackage &package, std::vector<UE_UPackage::Enum> &pkgEnums, std::vector<UE_UPackage::Struct> &pkgStructs, std::vector<UE_UPackage::Struct> &pkgClasses)
    {
        buffer.append("// Package: {}\n// Enums: {}\n// Structs: {}\n// Classes: {}\n\n",
                      package.GetObject().GetName(), pkgEnums.size(), pkgStructs.size(), pkgClasses.size());

        if (pkgEnums.size())
            UE_UPackage::AppendEnumsToBuffer(pkgEnums, &buffer);

        if (pkgStructs.size())
            UE_UPackage::AppendStructsToBuffer(pkgStructs, &buffer);

        if (pkgClasses.size())
            UE_UPackage::AppendStructsToBuffer(pkgClasses, &buffer);
    }
}  // namespace

bool UEDumper::Init(IGameProfile *profile)
{
    UEVarsInitStatus initStatus = profile->InitUEVars();
    if (initStatus != UEVarsInitStatus::SUCCESS)
    {
        _lastError = UEVars::InitStatusToStr(initStatus);
        return false;
    }
    _profile = profile;
    return true;
}

bool UEDumper::Dump(std::unordered_map<std::string, BufferFmt> *outBuffersMap)
{
    outBuffersMap->insert({"Logs.txt", BufferFmt()});
    BufferFmt &logsBufferFmt = outBuffersMap->at("Logs.txt");

    {
        if (_dumpExeInfoNotify) _dumpExeInfoNotify(false);
        DumpExecutableInfo(logsBufferFmt);
        if (_dumpExeInfoNotify) _dumpExeInfoNotify(true);
    }

    {
        if (_dumpNamesInfoNotify) _dumpNamesInfoNotify(false);
        DumpNamesInfo(logsBufferFmt);
        if (_dumpNamesInfoNotify) _dumpNamesInfoNotify(true);
    }

    {
        if (_dumpObjectsInfoNotify) _dumpObjectsInfoNotify(false);
        DumpObjectsInfo(logsBufferFmt);
        if (_dumpObjectsInfoNotify) _dumpObjectsInfoNotify(true);
    }

    {
        if (_dumpOffsetsInfoNotify) _dumpOffsetsInfoNotify(false);
        outBuffersMap->insert({"Offsets.hpp", BufferFmt()});
        BufferFmt &offsetsBufferFmt = outBuffersMap->at("Offsets.hpp");
        DumpOffsetsInfo(logsBufferFmt, offsetsBufferFmt);
        if (_dumpOffsetsInfoNotify) _dumpOffsetsInfoNotify(true);
    }

    outBuffersMap->insert({"Objects.txt", BufferFmt()});
    BufferFmt &objsBufferFmt = outBuffersMap->at("Objects.txt");
    std::vector<std::pair<uint8_t *const, std::vector<UE_UObject>>> packages;
    GatherUObjects(logsBufferFmt, objsBufferFmt, packages, _objectsProgressCallback);

    if (packages.empty())
    {
        logsBufferFmt.append("Error: Packages are empty.\n");
        logsBufferFmt.append("==========================\n");
        _lastError = "ERROR_EMPTY_PACKAGES";
        return false;
    }

    outBuffersMap->insert({"AIOHeader.hpp", BufferFmt()});
    BufferFmt &aioBufferFmt = outBuffersMap->at("AIOHeader.hpp");
    DumpAIOHeader(logsBufferFmt, aioBufferFmt, outBuffersMap, packages, _dumpProgressCallback);

    outBuffersMap->insert({"SDK.txt", BufferFmt()});
    outBuffersMap->at("SDK.txt").append("{}", aioBufferFmt.readView());

    dumper_jf_ns::base_address = _profile->GetUnrealELF().base();
    if (dumper_jf_ns::jsonFunctions.size())
    {
        logsBufferFmt.append("Generating script json...\nFunctions: {}\n", dumper_jf_ns::jsonFunctions.size());
        logsBufferFmt.append("==========================\n");

        outBuffersMap->insert({"script.json", BufferFmt()});
        BufferFmt &scriptBufferFmt = outBuffersMap->at("script.json");

        json js;
        for (const auto &jf : dumper_jf_ns::jsonFunctions)
        {
            js["Functions"].push_back(jf);
        }

        scriptBufferFmt.append("{}", js.dump(4));
    }

    return true;
}

void UEDumper::DumpExecutableInfo(BufferFmt &logsBufferFmt)
{
    auto ue_elf = _profile->GetUnrealELF();
    logsBufferFmt.append("e_machine: 0x{:X}\n", ue_elf.header().e_machine);
    logsBufferFmt.append("Library: {}\n", ue_elf.realPath().c_str());
    logsBufferFmt.append("BaseAddress: 0x{:X}\n", ue_elf.base());

    for (const auto &it : ue_elf.segments())
        logsBufferFmt.append("{}\n", it.toString());

    logsBufferFmt.append("==========================\n");
}

void UEDumper::DumpNamesInfo(BufferFmt &logsBufferFmt)
{
    uintptr_t baseAddr = _profile->GetUEVars()->GetBaseAddress();
    uintptr_t namesPtr = _profile->GetUEVars()->GetNamesPtr();

    if (!_profile->IsUsingFNamePool())
    {
        logsBufferFmt.append("GNames: [<Base> + 0x{:X}] = 0x{:X}\n",
                             namesPtr - baseAddr, namesPtr);
    }
    else
    {
        logsBufferFmt.append("FNamePool: [<Base> + 0x{:X}] = 0x{:X}\n",
                             namesPtr - baseAddr, namesPtr);
    }

    logsBufferFmt.append("Test dumping first 5 name entries\n");
    for (int i = 0; i < 5; i++)
    {
        std::string name = _profile->GetUEVars()->GetNameByID(i);
        LOGI("GetNameByID(%d): %s", i, name.c_str());
        logsBufferFmt.append("GetNameByID({}): {}\n", i, name);
    }

    logsBufferFmt.append("==========================\n");
}

void UEDumper::DumpObjectsInfo(BufferFmt &logsBufferFmt)
{
    uintptr_t baseAddr = _profile->GetUEVars()->GetBaseAddress();
    uintptr_t objectArrayPtr = _profile->GetUEVars()->GetGUObjectsArrayPtr();
    uintptr_t objObjectsPtr = _profile->GetUEVars()->GetObjObjectsPtr();

    logsBufferFmt.append("GUObjectArray: [<Base> + 0x{:X}] = 0x{:X}\n", objectArrayPtr - baseAddr, objectArrayPtr);
    logsBufferFmt.append("ObjObjects: [<Base> + 0x{:X}] = 0x{:X}\n", objObjectsPtr - baseAddr, objObjectsPtr);
    logsBufferFmt.append("ObjObjects Num: {}\n", UEWrappers::GetObjects()->GetNumElements());

    logsBufferFmt.append("Test Dumping First 5 Name Entries\n");
    for (int i = 0; i < 5; i++)
    {
        UE_UObject obj = UEWrappers::GetObjects()->GetObjectPtr(i);
        std::string name = obj.GetName();
        std::string fullName = obj.GetFullName();
        LOGI("GetObjectPtr(%d): %s | %s", i, name.c_str(), fullName.c_str());
        logsBufferFmt.append("GetObjectPtr({}): {}\n", i, name);
    }

    logsBufferFmt.append("==========================\n");
}

void UEDumper::DumpOffsetsInfo(BufferFmt &logsBufferFmt, BufferFmt &offsetsBufferFmt)
{
    uintptr_t baseAddr = _profile->GetUEVars()->GetBaseAddress();
    uintptr_t namesPtr = _profile->GetUEVars()->GetNamesPtr();
    uintptr_t objectsArrayPtr = _profile->GetUEVars()->GetGUObjectsArrayPtr();
    uintptr_t objObjectsPtr = _profile->GetUEVars()->GetObjObjectsPtr();
    uintptr_t UEnginePtr = 0, UWorldPtr = 0, ProcessEventPtr = 0;
    int ProcessEventIndex = 0;
    bool UEngineFromProfile = false, UWorldFromProfile = false;

    // Find UEngine & UWorld
    uint8_t *UEngineObj = nullptr, *UWorldObj = nullptr;
    if (((UE_UObject)UEWrappers::GetObjects()->GetObjectPtr(1)).GetIndex() == 1)
    {
        UE_UClass UEngineClass = UEWrappers::GetObjects()->FindObject("Class Engine.Engine").Cast<UE_UClass>();
        UE_UClass UWorldClass = UEWrappers::GetObjects()->FindObject("Class Engine.World").Cast<UE_UClass>();

        logsBufferFmt.append("Finding GEngine & GWorld...\n");
        logsBufferFmt.append("{} -> 0x{:X}\n", UEngineClass.GetFullName(), uintptr_t(UEngineClass.GetAddress()));
        logsBufferFmt.append("{} -> 0x{:X}\n", UWorldClass.GetFullName(), uintptr_t(UWorldClass.GetAddress()));

        if (UEngineClass || UWorldClass)
        {
            UEWrappers::GetObjects()->ForEachObject([&UEngineClass, &UWorldClass, &UEngineObj, &UWorldObj](UE_UObject object) -> bool
            {
                if (!object.HasFlags(EObjectFlags::ClassDefaultObject))
                {
                    bool isUEngine = UEngineClass && object.IsA(UEngineClass);
                    bool isUWorld = UWorldClass && object.IsA(UWorldClass);
                    if (isUEngine) UEngineObj = object.GetAddress();
                    if (isUWorld) UWorldObj = object.GetAddress();
                }
                return ((!UEngineClass || UEngineObj != nullptr) && (!UWorldClass || UWorldObj != nullptr));
            });
        }

        auto ueSegs = _profile->GetUnrealELF().segments();

        // reverse search, start with .bss
        for (auto it = ueSegs.begin(); it != ueSegs.end(); ++it)
        {
            if (!it->is_rw || it->startAddress == baseAddr)
                continue;

            std::vector<char> buffer(it->length, 0);
            vm_rpm_ptr((void *)it->startAddress, buffer.data(), buffer.size());

            UEnginePtr = FindAlignedPointerRefrence(it->startAddress, buffer, (uintptr_t)UEngineObj);
            UWorldPtr = FindAlignedPointerRefrence(it->startAddress, buffer, (uintptr_t)UWorldObj);

            if (UEnginePtr != 0 || UWorldPtr != 0)
                break;
        }

        logsBufferFmt.append("Finding ProcessEvent...\n");
        uint8_t *obj = UEngineObj ? UEngineObj : UWorldObj;
        if (!obj || !_profile->findProcessEvent(obj, &ProcessEventPtr, &ProcessEventIndex))
            logsBufferFmt.append("Couldn't find ProcessEvent.\n");
        else
            logsBufferFmt.append("ProcessEvent: Index({}) | [<Base> + 0x{:X}] = 0x{:X}\n", ProcessEventIndex, ProcessEventPtr - baseAddr, ProcessEventPtr);
    }

    if (!UEnginePtr)
    {
        uintptr_t profileUEnginePtr = _profile->GetGEnginePtr();
        if (profileUEnginePtr)
        {
            UEnginePtr = profileUEnginePtr;
            UEngineFromProfile = true;
        }
    }

    if (!UWorldPtr)
    {
        uintptr_t profileUWorldPtr = _profile->GetGWorldPtr();
        if (profileUWorldPtr)
        {
            UWorldPtr = profileUWorldPtr;
            UWorldFromProfile = true;
        }
    }

    if (!UEnginePtr)
        logsBufferFmt.append("Couldn't find refrence to GEngine.\n");
    else
        logsBufferFmt.append("GEngine: [<Base> + 0x{:X}] = 0x{:X}{}\n", UEnginePtr - baseAddr, UEnginePtr, UEngineFromProfile ? " (profile direct offset)" : "");

    if (!UWorldPtr)
        logsBufferFmt.append("Couldn't find refrence to GWorld.\n");
    else
        logsBufferFmt.append("GWorld: [<Base> + 0x{:X}] = 0x{:X}{}\n", UWorldPtr - baseAddr, UWorldPtr, UWorldFromProfile ? " (profile direct offset)" : "");

    UE_Pointers uEPointers{};
    uEPointers.Names = namesPtr - baseAddr;
    uEPointers.UObjectArray = objectsArrayPtr - baseAddr;
    uEPointers.ObjObjects = objObjectsPtr - baseAddr;
    uEPointers.Engine = UEnginePtr ? (UEnginePtr - baseAddr) : 0;
    uEPointers.World = UWorldPtr ? (UWorldPtr - baseAddr) : 0;
    uEPointers.ProcessEvent = ProcessEventPtr ? (ProcessEventPtr - baseAddr) : 0;
    uEPointers.ProcessEventIndex = ProcessEventIndex;

    offsetsBufferFmt.append("#pragma once\n\n#include <cstdint>\n\n\n");
    offsetsBufferFmt.append("{}\n\n{}", _profile->GetOffsets()->ToString(), uEPointers.ToString());
    std::string extraOffsetsHeader = _profile->GetExtraOffsetsHeader();
    if (!extraOffsetsHeader.empty())
        offsetsBufferFmt.append("\n\n{}", extraOffsetsHeader);

    logsBufferFmt.append("==========================\n");
}

void UEDumper::GatherUObjects(BufferFmt &logsBufferFmt, BufferFmt &objsBufferFmt, UEPackagesArray &packages, const ProgressCallback &progressCallback)
{
    logsBufferFmt.append("Gathering UObjects...\n");

    if (UEWrappers::GetObjects()->GetNumElements() <= 0)
    {
        LOGE("GatherUObjects failed: object count is <= 0.");
        logsBufferFmt.append("UEWrappers::GetObjects()->GetNumElements() <= 0\n");
        logsBufferFmt.append("==========================\n");
        return;
    }

    if (((UE_UObject)UEWrappers::GetObjects()->GetObjectPtr(1)).GetIndex() != 1)
    {
        LOGE("GatherUObjects failed: object index 1 validation failed.");
        logsBufferFmt.append("UEWrappers::GetObjects()->GetObjectPtr(1).GetIndex() != 1\n");
        logsBufferFmt.append("==========================\n");
        return;
    }

    int objectsCount = UEWrappers::GetObjects()->GetNumElements();
    LOGI("GatherUObjects object count: %d", objectsCount);
    SimpleProgressBar objectsProgress(objectsCount);
    if (progressCallback)
        progressCallback(objectsProgress);

    for (int i = 0; i < objectsCount; i++)
    {
        UE_UObject object = UEWrappers::GetObjects()->GetObjectPtr(i);
        if (object)
        {
            if (object.IsA<UE_UFunction>() || object.IsA<UE_UStruct>() || object.IsA<UE_UEnum>() || IsDumpableObjectByName(object))
            {
                bool found = false;
                auto packageObj = object.GetPackageObject();
                for (auto &pkg : packages)
                {
                    if (pkg.first == packageObj)
                    {
                        pkg.second.push_back(object);
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    packages.push_back(std::make_pair(packageObj, std::vector<UE_UObject>(1, object)));
                }
            }

            objsBufferFmt.append("[{:010}]: {}\n", object.GetIndex(), object.GetFullName());
        }

        objectsProgress++;
        if (progressCallback)
            progressCallback(objectsProgress);
    }

    logsBufferFmt.append("Gathered {} Objects (Packages {})\n", objectsCount, packages.size());
    logsBufferFmt.append("==========================\n");
}

void UEDumper::DumpAIOHeader(BufferFmt &logsBufferFmt, BufferFmt &aioBufferFmt, std::unordered_map<std::string, BufferFmt> *outBuffersMap, UEPackagesArray &packages, const ProgressCallback &progressCallback)
{
    int packages_saved = 0;
    int sdk_headers_saved = 0;
    std::string packages_unsaved{};

    int classes_saved = 0;
    int structs_saved = 0;
    int enums_saved = 0;

    static bool processInternal_once = false;

    AppendSDKPreamble(aioBufferFmt);

    SimpleProgressBar dumpProgress(int(packages.size()));
    if (progressCallback)
        progressCallback(dumpProgress);

    auto excludedObjects = _profile->GetExcludedObjects();

    for (UE_UPackage package : packages)
    {
        package.Process();

        dumpProgress++;
        if (progressCallback)
            progressCallback(dumpProgress);

        if (package.Classes.size() || package.Structures.size() || package.Enums.size())
        {
            auto pkgEnums = package.Enums;
            auto pkgStructs = package.Structures;
            auto pkgClasses = package.Classes;

            if (excludedObjects.size())
            {
                pkgEnums.erase(
                    std::remove_if(pkgEnums.begin(), pkgEnums.end(),
                                   [&excludedObjects](const UE_UPackage::Enum &it)
                { return kVECTOR_CONTAINS(excludedObjects, it.FullName); }),
                    pkgEnums.end());

                pkgStructs.erase(
                    std::remove_if(pkgStructs.begin(), pkgStructs.end(),
                                   [&excludedObjects](const UE_UPackage::Struct &it)
                { return kVECTOR_CONTAINS(excludedObjects, it.FullName); }),
                    pkgStructs.end());

                pkgClasses.erase(
                    std::remove_if(pkgClasses.begin(), pkgClasses.end(),
                                   [&excludedObjects](const UE_UPackage::Struct &it)
                { return kVECTOR_CONTAINS(excludedObjects, it.FullName); }),
                    pkgClasses.end());
            }

            if (pkgClasses.size() || pkgStructs.size() || pkgEnums.size())
            {
                AppendPackageSDK(aioBufferFmt, package, pkgEnums, pkgStructs, pkgClasses);

                if (outBuffersMap)
                {
                    std::string sdkPath = MakeSDKHeaderPath(package.GetObject().GetName());
                    auto insertResult = outBuffersMap->insert({sdkPath, BufferFmt()});
                    BufferFmt &sdkBufferFmt = insertResult.first->second;
                    if (insertResult.second)
                        AppendSDKPreamble(sdkBufferFmt);

                    AppendPackageSDK(sdkBufferFmt, package, pkgEnums, pkgStructs, pkgClasses);
                    sdk_headers_saved++;
                }
            }
            else
            {
                packages_unsaved += "\t";
                packages_unsaved += (package.GetObject().GetName() + ",\n");
                continue;
            }
        }
        else
        {
            packages_unsaved += "\t";
            packages_unsaved += (package.GetObject().GetName() + ",\n");
            continue;
        }

        packages_saved++;
        classes_saved += package.Classes.size();
        structs_saved += package.Structures.size();
        enums_saved += package.Enums.size();

        for (const auto &cls : package.Classes)
        {
            for (const auto &func : cls.Functions)
            {
                // UObject::ProcessInternal for blueprint functions
                if (!processInternal_once && (func.EFlags & FUNC_BlueprintEvent) && func.Func)
                {
                    dumper_jf_ns::jsonFunctions.push_back({"UObject", "ProcessInternal", func.Func});
                    processInternal_once = true;
                }

                if ((func.EFlags & FUNC_Native) && func.Func)
                {
                    std::string execFuncName = "exec";
                    execFuncName += func.Name;
                    dumper_jf_ns::jsonFunctions.push_back({cls.Name, execFuncName, func.Func});
                }
            }
        }

        for (const auto &st : package.Structures)
        {
            for (const auto &func : st.Functions)
            {
                if ((func.EFlags & FUNC_Native) && func.Func)
                {
                    std::string execFuncName = "exec";
                    execFuncName += func.Name;
                    dumper_jf_ns::jsonFunctions.push_back({st.Name, execFuncName, func.Func});
                }
            }
        }
    }

    logsBufferFmt.append("Saved packages: {}\nSaved SDK headers: {}\nSaved classes: {}\nSaved structs: {}\nSaved enums: {}\n", packages_saved, sdk_headers_saved, classes_saved, structs_saved, enums_saved);

    if (packages_unsaved.size())
    {
        packages_unsaved.erase(packages_unsaved.size() - 2);
        logsBufferFmt.append("Unsaved packages: [\n{}\n]\n", packages_unsaved);
    }

    logsBufferFmt.append("==========================\n");
}
