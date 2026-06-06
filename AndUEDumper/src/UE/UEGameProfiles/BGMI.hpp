#pragma once

#include <array>
#include <cctype>
#include <sstream>
#include <vector>

#include "PUBG.hpp"

namespace BGMIOptions
{
    inline constexpr uintptr_t kGNamesOffset = 0x9BFB714;
    inline constexpr uintptr_t kGNamesAdd = 0x88;
    inline constexpr uintptr_t kGNamesDirectOffset = kGNamesOffset + kGNamesAdd;
    inline constexpr uintptr_t kGNamesAltOffset = 0x4FC1620;
    inline constexpr uintptr_t kGUObjectOffset = 0x9DE3114;
    inline constexpr uintptr_t kGWorldOffset = 0x9FEB994 + 0x3C;

    inline uintptr_t GNames = kGNamesDirectOffset;
    inline uintptr_t GUObject = kGUObjectOffset;
    inline uintptr_t GWorld = kGWorldOffset;
}  // namespace BGMIOptions

class BGMIProfile : public PUBGProfile
{
public:
    BGMIProfile() = default;

    std::string GetAppName() const override
    {
        return "BGMI";
    }

    std::vector<std::string> GetAppIDs() const override
    {
        return {"com.pubg.imobile"};
    }

    bool IsUsingFNamePool() const override
    {
        return false;
    }

    UE_Offsets *GetOffsets() const override
    {
        static UE_Offsets offsets = UE_DefaultOffsets::UE4_18_19(isUsingCasePreservingName());
        static bool once = false;
        if (!once)
        {
            once = true;
            offsets.FNameEntry.Index = sizeof(void *);
            offsets.FNameEntry.Name = sizeof(void *) + sizeof(int32_t);
        }

        return &offsets;
    }

    uintptr_t GetGUObjectArrayPtr() const override
    {
        uintptr_t address = ResolveUserAddress(BGMIOptions::GUObject);
        LOGI("BGMI GUObjectArray direct address: %p", (void *)address);
        return address;
    }

    uintptr_t GetNamesPtr() const override
    {
        uintptr_t address = ResolveUserAddress(BGMIOptions::GNames);
        LOGI("BGMI GNames direct address: %p", (void *)address);
        return address;
    }

    uintptr_t GetGWorldPtr() const override
    {
        uintptr_t address = ResolveUserAddress(BGMIOptions::GWorld);
        LOGI("BGMI GWorld direct address: %p", (void *)address);
        return address;
    }

    std::string GetExtraOffsetsHeader() const override
    {
        std::ostringstream oss;
        oss << std::hex << std::uppercase;
        oss << "namespace BGMIOffsets\n{";
        AppendOffset(oss, "GNames", BGMIOptions::GNames);
        AppendOffset(oss, "GUObject", BGMIOptions::GUObject);
        AppendOffset(oss, "GWorld", BGMIOptions::GWorld);
        oss << "\n}";
        return oss.str();
    }

protected:
    std::string GetNameByID(int32_t id) const override
    {
        if (id < 0)
            return "";

        NameCandidate candidate = ResolveNameCandidate();
        if (candidate.Score < 20)
            return "";

        return DecodeName(candidate, id);
    }

private:
    enum class NameKind
    {
        None,
        GNames,
        FNamePool,
        FlatPtrArray,
        FlatEntryArray,
    };

    struct NameCandidate
    {
        NameKind Kind = NameKind::None;
        const char *Label = "";
        uintptr_t Source = 0;
        uintptr_t Base = 0;
        uintptr_t NameOffset = 0;
        uintptr_t BlocksOff = 0;
        uint8_t LenShift = 0;
        uintptr_t Stride = 0;
        uintptr_t EntryStride = 0;
        int Score = 0;
        int KnownSeeds = 0;
        bool HasNoneAtZero = false;
        std::array<std::string, 5> Sample{};
        std::array<std::string, 5> FirstValid{};
    };

    uintptr_t ResolveUserAddress(uintptr_t addressOrOffset) const
    {
        if (addressOrOffset == 0)
            return 0;

        auto ue_elf = GetUnrealELF();
        if (!ue_elf.isValid() || ue_elf.base() == 0)
            return addressOrOffset;

        if (IsReadableAddress(addressOrOffset))
            return addressOrOffset;

        uintptr_t baseRelative = ue_elf.base() + addressOrOffset;
        if (IsReadableAddress(baseRelative))
            return baseRelative;

        return addressOrOffset;
    }

    bool IsReadableAddress(uintptr_t address) const
    {
        return address != 0 && kPtrValidator.isPtrReadable((void *)address);
    }

    NameCandidate ResolveNameCandidate() const
    {
        static NameCandidate best{};
        static bool once = false;
        if (once)
            return best;
        once = true;

        uintptr_t gNamesDirect = ResolveUserAddress(BGMIOptions::GNames);
        uintptr_t gNamesBase = ResolveUserAddress(BGMIOptions::kGNamesOffset);
        uintptr_t gNamesAlt = ResolveUserAddress(BGMIOptions::kGNamesAltOffset);

        std::vector<uintptr_t> sources;
        AddCandidateAddress(sources, gNamesDirect);
        AddCandidateAddress(sources, gNamesBase);
        AddCandidateAddress(sources, gNamesAlt);

        LOGI("BGMI GNames sources: direct=%p raw=0x%X:%p alt=0x%X:%p",
             (void *)gNamesDirect,
             (unsigned)BGMIOptions::kGNamesOffset,
             (void *)gNamesBase,
             (unsigned)BGMIOptions::kGNamesAltOffset,
             (void *)gNamesAlt);

        if (TryPreferredNames(best, gNamesBase))
        {
            LOGI("BGMI selected names %s/%s: source=%p base=%p nameOff=0x%X blocksOff=0x%X stride=0x%X entryStride=0x%X lenShift=%u score=%d seeds=%d none0=%s sample=[%s,%s,%s,%s,%s] firstValid=[%s,%s,%s,%s,%s]",
                 best.Label,
                 KindName(best.Kind),
                 (void *)best.Source,
                 (void *)best.Base,
                 (unsigned)best.NameOffset,
                 (unsigned)best.BlocksOff,
                 (unsigned)best.Stride,
                 (unsigned)best.EntryStride,
                 (unsigned)best.LenShift,
                 best.Score,
                 best.KnownSeeds,
                 best.HasNoneAtZero ? "true" : "false",
                 best.Sample[0].c_str(),
                 best.Sample[1].c_str(),
                 best.Sample[2].c_str(),
                 best.Sample[3].c_str(),
                 best.Sample[4].c_str(),
                 best.FirstValid[0].c_str(),
                 best.FirstValid[1].c_str(),
                 best.FirstValid[2].c_str(),
                 best.FirstValid[3].c_str(),
                 best.FirstValid[4].c_str());
            return best;
        }

        for (uintptr_t source : sources)
        {
            if (!IsReadableAddress(source))
                continue;

            std::vector<std::pair<const char *, uintptr_t>> bases;
            AddCandidateBase(bases, "source", source);
            if (source > BGMIOptions::kGNamesAdd)
                AddCandidateBase(bases, "source-0x88", source - BGMIOptions::kGNamesAdd);
            AddCandidateBase(bases, "source+0x88", source + BGMIOptions::kGNamesAdd);

            uintptr_t deref = ReadPtr(source);
            AddCandidateBase(bases, "[source]", deref);
            AddCandidateBase(bases, "[source]+0x88", deref + BGMIOptions::kGNamesAdd);
            AddCandidateBase(bases, "[[source]]", ReadPtr(deref));
            AddCandidateBase(bases, "[[source]+0x88]", ReadPtr(deref + BGMIOptions::kGNamesAdd));

            uintptr_t derefAdd = ReadPtr(source + BGMIOptions::kGNamesAdd);
            AddCandidateBase(bases, "[source+0x88]", derefAdd);
            AddCandidateBase(bases, "[[source+0x88]]", ReadPtr(derefAdd));

            for (const auto &base : bases)
            {
                TestGNamesBase(best, source, base.second, base.first);
                TestFNamePoolBase(best, source, base.second, base.first);
                TestFlatPtrArrayBase(best, source, base.second, base.first);
                TestFlatEntryArrayBase(best, source, base.second, base.first);
            }
        }

        if (best.Score >= 20)
        {
            LOGI("BGMI selected names %s/%s: source=%p base=%p nameOff=0x%X blocksOff=0x%X stride=0x%X entryStride=0x%X lenShift=%u score=%d seeds=%d none0=%s sample=[%s,%s,%s,%s,%s] firstValid=[%s,%s,%s,%s,%s]",
                 best.Label,
                 KindName(best.Kind),
                 (void *)best.Source,
                 (void *)best.Base,
                 (unsigned)best.NameOffset,
                 (unsigned)best.BlocksOff,
                 (unsigned)best.Stride,
                 (unsigned)best.EntryStride,
                 (unsigned)best.LenShift,
                 best.Score,
                 best.KnownSeeds,
                 best.HasNoneAtZero ? "true" : "false",
                 best.Sample[0].c_str(),
                 best.Sample[1].c_str(),
                 best.Sample[2].c_str(),
                 best.Sample[3].c_str(),
                 best.Sample[4].c_str(),
                 best.FirstValid[0].c_str(),
                 best.FirstValid[1].c_str(),
                 best.FirstValid[2].c_str(),
                 best.FirstValid[3].c_str(),
                 best.FirstValid[4].c_str());
        }
        else
        {
            LOGW("BGMI could not resolve valid names. best=%s/%s source=%p base=%p nameOff=0x%X blocksOff=0x%X stride=0x%X entryStride=0x%X lenShift=%u score=%d seeds=%d none0=%s sample=[%s,%s,%s,%s,%s] firstValid=[%s,%s,%s,%s,%s]",
                 best.Label,
                 KindName(best.Kind),
                 (void *)best.Source,
                 (void *)best.Base,
                 (unsigned)best.NameOffset,
                 (unsigned)best.BlocksOff,
                 (unsigned)best.Stride,
                 (unsigned)best.EntryStride,
                 (unsigned)best.LenShift,
                 best.Score,
                 best.KnownSeeds,
                 best.HasNoneAtZero ? "true" : "false",
                 best.Sample[0].c_str(),
                 best.Sample[1].c_str(),
                 best.Sample[2].c_str(),
                 best.Sample[3].c_str(),
                 best.Sample[4].c_str(),
                 best.FirstValid[0].c_str(),
                 best.FirstValid[1].c_str(),
                 best.FirstValid[2].c_str(),
                 best.FirstValid[3].c_str(),
                 best.FirstValid[4].c_str());

            LogNameDiagnostics(sources);
        }

        return best;
    }

    uintptr_t ReadPtr(uintptr_t address) const
    {
        if (!IsReadableAddress(address))
            return 0;

        return vm_rpm_ptr<uintptr_t>((void *)address);
    }

    bool TryPreferredNames(NameCandidate &best, uintptr_t gNamesRaw) const
    {
        uintptr_t first = ReadPtr(gNamesRaw);
        uintptr_t base = ReadPtr(first + BGMIOptions::kGNamesAdd);
        if (!IsReadableAddress(base))
            return false;

        NameCandidate candidate{};
        candidate.Kind = NameKind::GNames;
        candidate.Label = "builtin [[raw]+0x88]";
        candidate.Source = gNamesRaw;
        candidate.Base = base;
        candidate.NameOffset = 8;
        candidate.Score = ScoreCandidate(candidate);

        if (candidate.Score < 20)
            return false;

        best = candidate;
        return true;
    }

    void AddCandidateAddress(std::vector<uintptr_t> &arr, uintptr_t value) const
    {
        if (value == 0)
            return;

        for (uintptr_t existing : arr)
            if (existing == value)
                return;

        arr.push_back(value);
    }

    void AddCandidateBase(std::vector<std::pair<const char *, uintptr_t>> &arr, const char *label, uintptr_t value) const
    {
        if (!IsReadableAddress(value))
            return;

        for (const auto &existing : arr)
            if (existing.second == value)
                return;

        arr.push_back({label, value});
    }

    void TestGNamesBase(NameCandidate &best, uintptr_t source, uintptr_t base, const char *label) const
    {
        std::array<uintptr_t, 7> nameOffsets = {0, 4, 8, 0xC, 0x10, 0x14, 0x18};
        for (uintptr_t nameOffset : nameOffsets)
        {
            NameCandidate candidate{};
            candidate.Kind = NameKind::GNames;
            candidate.Label = label;
            candidate.Source = source;
            candidate.Base = base;
            candidate.NameOffset = nameOffset;
            candidate.Score = ScoreCandidate(candidate);

            if (candidate.Score > best.Score)
                best = candidate;
        }
    }

    void TestFNamePoolBase(NameCandidate &best, uintptr_t source, uintptr_t base, const char *label) const
    {
        std::array<uintptr_t, 5> blocksOffsets = {0, 0x30, 0x40, BGMIOptions::kGNamesAdd, 0xD0};
        std::array<uintptr_t, 2> headerOffsets = {0, 4};
        std::array<uintptr_t, 2> strides = {2, 4};
        std::array<uint8_t, 2> lenShifts = {6, 1};

        for (uintptr_t blocksOff : blocksOffsets)
        {
            for (uintptr_t headerOff : headerOffsets)
            {
                for (uintptr_t stride : strides)
                {
                    for (uint8_t lenShift : lenShifts)
                    {
                        NameCandidate candidate{};
                        candidate.Kind = NameKind::FNamePool;
                        candidate.Label = label;
                        candidate.Source = source;
                        candidate.Base = base;
                        candidate.NameOffset = headerOff;
                        candidate.BlocksOff = blocksOff;
                        candidate.Stride = stride;
                        candidate.LenShift = lenShift;
                        candidate.Score = ScoreCandidate(candidate);

                        if (candidate.Score > best.Score)
                            best = candidate;
                    }
                }
            }
        }
    }

    void TestFlatPtrArrayBase(NameCandidate &best, uintptr_t source, uintptr_t base, const char *label) const
    {
        std::array<uintptr_t, 7> nameOffsets = {0, 4, 8, 0xC, 0x10, 0x14, 0x18};
        for (uintptr_t nameOffset : nameOffsets)
        {
            NameCandidate candidate{};
            candidate.Kind = NameKind::FlatPtrArray;
            candidate.Label = label;
            candidate.Source = source;
            candidate.Base = base;
            candidate.NameOffset = nameOffset;
            candidate.Score = ScoreCandidate(candidate);

            if (candidate.Score > best.Score)
                best = candidate;
        }
    }

    void TestFlatEntryArrayBase(NameCandidate &best, uintptr_t source, uintptr_t base, const char *label) const
    {
        std::array<uintptr_t, 7> nameOffsets = {0, 4, 8, 0xC, 0x10, 0x14, 0x18};
        std::array<uintptr_t, 7> entryStrides = {0x10, 0x14, 0x18, 0x20, 0x24, 0x28, 0x30};

        for (uintptr_t entryStride : entryStrides)
        {
            for (uintptr_t nameOffset : nameOffsets)
            {
                NameCandidate candidate{};
                candidate.Kind = NameKind::FlatEntryArray;
                candidate.Label = label;
                candidate.Source = source;
                candidate.Base = base;
                candidate.NameOffset = nameOffset;
                candidate.EntryStride = entryStride;
                candidate.Score = ScoreCandidate(candidate);

                if (candidate.Score > best.Score)
                    best = candidate;
            }
        }
    }

    int ScoreCandidate(NameCandidate &candidate) const
    {
        int score = 0;
        int firstValidIndex = 0;
        for (int32_t id = 0; id < 512; id++)
        {
            std::string name = DecodeName(candidate, id);
            if (!IsSaneName(name))
                continue;

            if (id < (int32_t)candidate.Sample.size())
                candidate.Sample[id] = name;

            if (firstValidIndex < (int)candidate.FirstValid.size())
                candidate.FirstValid[firstValidIndex++] = name;

            if (id == 0 && name == "None")
            {
                candidate.HasNoneAtZero = true;
                score += 200;
            }

            if (IsKnownUESeedName(name))
            {
                candidate.KnownSeeds++;
                score += 50;
            }

            if (name.size() >= 3)
                score++;
        }

        if (!candidate.HasNoneAtZero && candidate.KnownSeeds < 2)
            return 0;

        return score;
    }

    std::string DecodeName(const NameCandidate &candidate, int32_t id) const
    {
        if (candidate.Kind == NameKind::GNames)
            return DecodeGNamesName(candidate, id);

        if (candidate.Kind == NameKind::FNamePool)
            return DecodeFNamePoolName(candidate, id);

        if (candidate.Kind == NameKind::FlatPtrArray)
            return DecodeFlatPtrArrayName(candidate, id);

        if (candidate.Kind == NameKind::FlatEntryArray)
            return DecodeFlatEntryArrayName(candidate, id);

        return "";
    }

    std::string DecodeGNamesName(const NameCandidate &candidate, int32_t id) const
    {
        uint8_t *entry = GetGNamesEntry(candidate.Base, id);
        if (!entry)
            return "";

        return vm_rpm_str(entry + candidate.NameOffset, kMAX_UENAME_BUFFER);
    }

    uint8_t *GetGNamesEntry(uintptr_t gNames, int32_t id) const
    {
        const int32_t ElementsPerChunk = 16384;
        const int32_t ChunkIndex = id / ElementsPerChunk;
        const int32_t WithinChunkIndex = id % ElementsPerChunk;

        uint8_t *chunk = vm_rpm_ptr<uint8_t *>((void *)(gNames + ChunkIndex * sizeof(uintptr_t)));
        if (!chunk)
            return nullptr;

        return vm_rpm_ptr<uint8_t *>(chunk + WithinChunkIndex * sizeof(uintptr_t));
    }

    std::string DecodeFNamePoolName(const NameCandidate &candidate, int32_t id) const
    {
        uint8_t *entry = GetFNamePoolEntry(candidate, id);
        if (!entry)
            return "";

        uint8_t *headerPtr = entry + candidate.NameOffset;
        uint16_t header = 0;
        if (!vm_rpm_ptr(headerPtr, &header, sizeof(header)))
            return "";

        size_t len = header >> candidate.LenShift;
        if (len == 0 || len > kMAX_UENAME_BUFFER)
            return "";

        return vm_rpm_str(headerPtr + sizeof(uint16_t), len);
    }

    uint8_t *GetFNamePoolEntry(const NameCandidate &candidate, int32_t id) const
    {
        constexpr uintptr_t blockBits = 16;
        uintptr_t block = (id >> blockBits) * sizeof(void *);
        uintptr_t offset = (id & ((1 << blockBits) - 1)) * candidate.Stride;

        uint8_t *chunk = vm_rpm_ptr<uint8_t *>((void *)(candidate.Base + candidate.BlocksOff + block));
        if (!chunk)
            return nullptr;

        return chunk + offset;
    }

    std::string DecodeFlatPtrArrayName(const NameCandidate &candidate, int32_t id) const
    {
        uint8_t *entry = vm_rpm_ptr<uint8_t *>((void *)(candidate.Base + (id * sizeof(uintptr_t))));
        if (!entry)
            return "";

        return vm_rpm_str(entry + candidate.NameOffset, kMAX_UENAME_BUFFER);
    }

    std::string DecodeFlatEntryArrayName(const NameCandidate &candidate, int32_t id) const
    {
        uintptr_t entry = candidate.Base + (id * candidate.EntryStride);
        if (!IsReadableAddress(entry + candidate.NameOffset))
            return "";

        return vm_rpm_str((uint8_t *)entry + candidate.NameOffset, kMAX_UENAME_BUFFER);
    }

    void LogNameDiagnostics(const std::vector<uintptr_t> &sources) const
    {
        for (uintptr_t source : sources)
        {
            uintptr_t p0 = ReadPtr(source);
            uintptr_t p88 = ReadPtr(source + BGMIOptions::kGNamesAdd);
            uintptr_t p0p88 = ReadPtr(p0 + BGMIOptions::kGNamesAdd);
            uintptr_t pp0p88 = ReadPtr(p0p88);

            LOGI("BGMI diag source=%p [source]=%p [source+0x88]=%p [[source]+0x88]=%p [[[source]+0x88]]=%p",
                 (void *)source,
                 (void *)p0,
                 (void *)p88,
                 (void *)p0p88,
                 (void *)pp0p88);

            LogNameProbe("source", source);
            LogNameProbe("source+0x88", source + BGMIOptions::kGNamesAdd);
            LogNameProbe("[source]", p0);
            LogNameProbe("[source]+0x88", p0 + BGMIOptions::kGNamesAdd);
            LogNameProbe("[[source]+0x88]", p0p88);
        }
    }

    void LogNameProbe(const char *label, uintptr_t base) const
    {
        if (!IsReadableAddress(base))
            return;

        uintptr_t flat0 = ReadPtr(base);
        uintptr_t flat1 = ReadPtr(base + sizeof(uintptr_t));
        uintptr_t chunk0 = flat0;
        uintptr_t chunkEntry0 = ReadPtr(chunk0);

        LOGI("BGMI probe %s base=%p ptr0=%p ptr1=%p chunkEntry0=%p flat0=[%s|%s|%s] chunk0=[%s|%s|%s]",
             label,
             (void *)base,
             (void *)flat0,
             (void *)flat1,
             (void *)chunkEntry0,
             PreviewString(flat0 + 0).c_str(),
             PreviewString(flat0 + 4).c_str(),
             PreviewString(flat0 + 8).c_str(),
             PreviewString(chunkEntry0 + 0).c_str(),
             PreviewString(chunkEntry0 + 4).c_str(),
             PreviewString(chunkEntry0 + 8).c_str());
    }

    bool IsKnownUESeedName(const std::string &name) const
    {
        return name == "None" ||
               name == "ByteProperty" ||
               name == "IntProperty" ||
               name == "BoolProperty" ||
               name == "FloatProperty" ||
               name == "ObjectProperty" ||
               name == "NameProperty" ||
               name == "DelegateProperty" ||
               name == "ClassProperty" ||
               name == "ArrayProperty" ||
               name == "StructProperty" ||
               name == "StrProperty" ||
               name == "TextProperty" ||
               name == "MapProperty" ||
               name == "SetProperty" ||
               name == "UInt64Property" ||
               name == "Int64Property" ||
               name == "DoubleProperty" ||
               name == "Object";
    }

    bool IsSaneName(const std::string &name) const
    {
        if (name.empty() || name.size() > 128)
            return false;

        if (name.size() < 3 && !IsKnownUESeedName(name))
            return false;

        bool hasAlpha = false;
        for (char c : name)
        {
            unsigned char uc = static_cast<unsigned char>(c);
            if (std::isalpha(uc))
                hasAlpha = true;

            if (!(std::isalnum(uc) || c == '_' || c == '/' || c == '.' || c == ':' || c == '-'))
                return false;
        }

        return hasAlpha;
    }

    const char *KindName(NameKind kind) const
    {
        switch (kind)
        {
            case NameKind::GNames:
                return "GNames";
            case NameKind::FNamePool:
                return "FNamePool";
            case NameKind::FlatPtrArray:
                return "FlatPtrArray";
            case NameKind::FlatEntryArray:
                return "FlatEntryArray";
            default:
                return "None";
        }
    }

    std::string PreviewString(uintptr_t address) const
    {
        if (!IsReadableAddress(address))
            return "";

        return SanitizeForLog(vm_rpm_str((void *)address, 32));
    }

    std::string SanitizeForLog(const std::string &value) const
    {
        std::string result;
        result.reserve(value.size());

        for (char c : value)
        {
            unsigned char uc = static_cast<unsigned char>(c);
            if (std::isprint(uc))
                result.push_back(c);
            else
                result.push_back('.');
        }

        return result;
    }

    static void AppendOffset(std::ostringstream &oss, const char *name, uintptr_t value)
    {
        oss << "\n    constexpr uintptr_t " << name << " = 0x" << value << ";";
    }
};
