// AppCenter overlay transport prototype. Deliberately not included by the
// production helper until Windows ACL/lifecycle/secure-desktop tests pass.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace HelperOverlayPolicy {
constexpr std::size_t WireSize = 576;
constexpr std::uint32_t PublicationWord = 0x4f484341;
struct Snapshot {
    std::uint64_t sessionId = 0;
    std::uint64_t expiresAt = 0;
    bool enabled = false;
    std::uint16_t operatorName[256] = {};
};

inline std::uint32_t U32(const unsigned char* p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 |
        std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
inline std::uint64_t U64(const unsigned char* p) {
    return std::uint64_t(U32(p)) | std::uint64_t(U32(p + 4)) << 32;
}

// No pointers, handles, password or executable command in the wire contract.
inline bool Parse(const unsigned char* data, std::size_t size,
    std::uint64_t now, Snapshot& result) {
    result = Snapshot{};
    const unsigned char magic[8] = {'A', 'C', 'H', 'O', 'V', 'L', '1', 0};
    if (!data || size != WireSize || std::memcmp(data, magic, 8) ||
        U32(data + 8) != 1 || U32(data + 12) != WireSize)
        return false;
    Snapshot candidate;
    candidate.sessionId = U64(data + 16);
    candidate.expiresAt = U64(data + 24);
    const auto flags = U32(data + 32);
    const auto count = U32(data + 36);
    if (!candidate.sessionId || candidate.expiresAt <= now ||
        candidate.expiresAt > 0x7fffffffffffffffULL || flags > 1 || count > 255 ||
        (!flags && count))
        return false;
    for (std::uint32_t i = 0; i < count; ++i)
        candidate.operatorName[i] = std::uint16_t(data[40 + i * 2]) |
            std::uint16_t(data[41 + i * 2]) << 8;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto unit = candidate.operatorName[i];
        if (!unit) return false;
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (i + 1 >= count || candidate.operatorName[i + 1] < 0xdc00 ||
                candidate.operatorName[i + 1] > 0xdfff) return false;
            ++i;
        } else if (unit >= 0xdc00 && unit <= 0xdfff) return false;
    }
    for (std::size_t i = 40 + count * 2; i < WireSize; ++i)
        if (data[i]) return false;
    candidate.enabled = flags == 1;
    result = candidate;
    return true;
}
} // namespace HelperOverlayPolicy

#ifdef _WIN32
#include <windows.h>
#include <aclapi.h>
#pragma comment(lib, "advapi32.lib")

namespace HelperOverlayPolicy {
enum class ReadResult { Ready, Absent, Invalid };

// A global name alone is not authentication. Require SYSTEM ownership and the
// exact writer/reader principal set before trusting an overlay-off instruction.
inline bool TrustedMapping(HANDLE mapping) {
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetSecurityInfo(mapping, SE_KERNEL_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, nullptr, &dacl, nullptr, &descriptor) != ERROR_SUCCESS)
        return false;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    bool trusted = owner && IsWellKnownSid(owner, WinLocalSystemSid) && dacl &&
        dacl->AceCount == 3 && GetSecurityDescriptorControl(descriptor, &control, &revision) &&
        (control & SE_DACL_PROTECTED);
    bool system = false, admin = false, readers = false;
    for (DWORD i = 0; trusted && i < dacl->AceCount; ++i) {
        void* raw = nullptr;
        if (!GetAce(dacl, i, &raw)) { trusted = false; break; }
        const auto ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || ace->Header.AceFlags) {
            trusted = false; break;
        }
        PSID sid = const_cast<DWORD*>(&ace->SidStart);
        // Depending on kernel generic-rights mapping, the full-access ACE is
        // represented as GENERIC_ALL or concrete SECTION_ALL_ACCESS.
        const bool fullAccess = ace->Mask == GENERIC_ALL || ace->Mask == FILE_MAP_ALL_ACCESS;
        if (IsWellKnownSid(sid, WinLocalSystemSid) && !system && fullAccess) system = true;
        else if (IsWellKnownSid(sid, WinBuiltinAdministratorsSid) && !admin && fullAccess) admin = true;
        else if (IsWellKnownSid(sid, WinAuthenticatedUserSid) && !readers &&
            ace->Mask == (READ_CONTROL | FILE_MAP_READ)) readers = true;
        else trusted = false;
    }
    LocalFree(descriptor);
    return trusted && system && admin && readers;
}

// Called once at desktop/overlay initialization, not from a timer/frame loop.
// Missing/invalid policy must never be interpreted as an explicit disabled flag.
inline ReadResult ReadNamed(const wchar_t* mappingName, Snapshot& result) {
    result = Snapshot{};
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ | READ_CONTROL, FALSE,
        mappingName);
    if (!mapping)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? ReadResult::Absent : ReadResult::Invalid;
    if (!TrustedMapping(mapping)) { CloseHandle(mapping); return ReadResult::Invalid; }
    const auto view = static_cast<const unsigned char*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, WireSize));
    if (!view) { CloseHandle(mapping); return ReadResult::Invalid; }
    unsigned char copy[WireSize] = {};
    const auto publication = reinterpret_cast<const volatile DWORD*>(view);
    bool published = *publication == PublicationWord;
    MemoryBarrier();
    if (published) std::memcpy(copy, view, WireSize);
    MemoryBarrier();
    published = published && *publication == PublicationWord;
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    FILETIME time;
    GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER ticks;
    ticks.LowPart = time.dwLowDateTime;
    ticks.HighPart = time.dwHighDateTime;
    const auto now = ticks.QuadPart / 10000000ULL - 11644473600ULL;
    const bool valid = published && Parse(copy, WireSize, now, result);
    SecureZeroMemory(copy, sizeof(copy));
    return valid ? ReadResult::Ready : ReadResult::Invalid;
}

inline ReadResult Read(Snapshot& result) {
    return ReadNamed(L"Global\\AppCenter.HelperOverlay.v1", result);
}
} // namespace HelperOverlayPolicy
#endif
