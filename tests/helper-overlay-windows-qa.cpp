// Isolated native overlay transport acceptance harness. No VNC/service changes.
#include "../winvnc/winvnc/HelperOverlayPolicy.h"
#include <sddl.h>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace HelperOverlayPolicy;
namespace {
const wchar_t* ProductionName = L"Global\\AppCenter.HelperOverlay.v1";
const wchar_t* PolicySDDL = L"O:SYG:SYD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00020004;;;AU)";
void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(std::string(message) + " (win32=" + std::to_string(GetLastError()) + ")");
}
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct Mapping {
    Handle handle;
    void* view = nullptr;
    std::wstring name;
    ~Mapping() { if (view) UnmapViewOfFile(view); }
    Mapping(const wchar_t* sddl) {
        static unsigned counter = 0;
        name = L"Local\\AppCenter.HelperOverlay.QA." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++counter);
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        Require(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, nullptr) != FALSE, "convert test ACL");
        SECURITY_ATTRIBUTES sa = {sizeof(sa), descriptor, FALSE};
        handle.value = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, static_cast<DWORD>(WireSize), name.c_str());
        const DWORD createError = GetLastError();
        LocalFree(descriptor);
        Require(handle.value && createError != ERROR_ALREADY_EXISTS, "create unique test mapping");
        view = MapViewOfFile(handle.value, FILE_MAP_WRITE, 0, 0, WireSize);
        Require(view != nullptr, "map test writer");
    }
};
void Put32(unsigned char* p, std::uint32_t v) { for (int i=0;i<4;++i) p[i]=static_cast<unsigned char>(v>>(i*8)); }
void Put64(unsigned char* p, std::uint64_t v) { Put32(p,static_cast<std::uint32_t>(v));Put32(p+4,static_cast<std::uint32_t>(v>>32)); }
std::uint64_t Now() {
    FILETIME time; GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER ticks; ticks.LowPart=time.dwLowDateTime;ticks.HighPart=time.dwHighDateTime;
    return ticks.QuadPart/10000000ULL-11644473600ULL;
}
void Publish(Mapping& m) {
    unsigned char wire[WireSize] = {};
    const unsigned char magic[8]={'A','C','H','O','V','L','1',0};
    std::memcpy(wire,magic,8);
    Put32(wire+8,1);Put32(wire+12,static_cast<std::uint32_t>(WireSize));
    Put64(wire+16,42);Put64(wire+24,Now()+60);Put32(wire+32,1);
    const std::wstring text=L"QA-İpek ŞEN \"Destek\"";
    Put32(wire+36,static_cast<std::uint32_t>(text.size()));
    std::memcpy(wire+40,text.data(),text.size()*sizeof(wchar_t));
    std::memcpy(static_cast<unsigned char*>(m.view)+4,wire+4,WireSize-4);
    InterlockedExchange(static_cast<volatile LONG*>(m.view), static_cast<LONG>(PublicationWord));
}
const char* State(ReadResult state) {
    return state==ReadResult::Ready?"Ready":state==ReadResult::Absent?"Absent":"Invalid";
}
std::string Escaped(const std::wstring& text) {
    std::string out;
    for (wchar_t unit:text) {
        if (unit>=32 && unit<127 && unit!=L'"' && unit!=L'\\') out+=static_cast<char>(unit);
        else { char hex[7];sprintf_s(hex,"\\u%04x",static_cast<unsigned>(unit));out+=hex; }
    }
    return out;
}
void PrintSnapshot(ReadResult state,const Snapshot& s,bool showTestOperator) {
    const std::wstring name(reinterpret_cast<const wchar_t*>(s.operatorName));
    const bool safe = showTestOperator && name.compare(0,3,L"QA-")==0;
    std::cout<<"{\"state\":\""<<State(state)<<"\",\"session_id\":"<<s.sessionId
        <<",\"expires_at\":"<<s.expiresAt<<",\"enabled\":"<<(s.enabled?"true":"false")
        <<",\"operator\":\""<<(name.empty()?"":safe?Escaped(name):"[redacted]")<<"\"}"<<std::endl;
}
void CheckAccess(const wchar_t* name) {
    Handle reader;
    reader.value=OpenFileMappingW(FILE_MAP_READ|READ_CONTROL,FALSE,name);
    Require(reader.value!=nullptr,"authenticated read/query denied");
    Require(TrustedMapping(reader.value),"reader rejected owner/DACL");
    Handle writer;
    writer.value=OpenFileMappingW(FILE_MAP_WRITE,FALSE,name);
    const DWORD error=GetLastError();
    Require(!writer.value && error==ERROR_ACCESS_DENIED,"authenticated writer was not denied");
    std::cout<<"{\"read_and_security_query\":true,\"write_denied\":true}"<<std::endl;
}
void RestrictedAccess(const wchar_t* name) {
    Handle token, restricted;
    Require(OpenProcessToken(GetCurrentProcess(),TOKEN_DUPLICATE|TOKEN_QUERY,&token.value)!=FALSE,"open token");
    unsigned char auBuffer[SECURITY_MAX_SID_SIZE];DWORD size=sizeof(auBuffer);
    Require(CreateWellKnownSid(WinAuthenticatedUserSid,nullptr,auBuffer,&size)!=FALSE,"make AU SID");
    SID_AND_ATTRIBUTES au={auBuffer,0};
    // A second access check is restricted to AU: SYSTEM/Admin membership in
    // the original token cannot grant data-write access during this test.
    Require(CreateRestrictedToken(token.value,DISABLE_MAX_PRIVILEGE,0,nullptr,0,nullptr,1,&au,&restricted.value)!=FALSE,"restrict token to AU access");
    Require(ImpersonateLoggedOnUser(restricted.value)!=FALSE,"impersonate restricted reader");
    try { CheckAccess(name); }
    catch (...) { RevertToSelf();throw; }
    Require(RevertToSelf()!=FALSE,"revert reader token");
}
void TestUntrusted() {
    Snapshot s;
    Mapping permissive(L"O:SYG:SYD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;AU)");
    Publish(permissive);
    Require(ReadNamed(permissive.name.c_str(),s)==ReadResult::Invalid,"accepted AU-write DACL");
    // Builtin Administrators is a valid owner group for an elevated SYSTEM
    // token but must not be accepted as the trusted SYSTEM-owned publisher.
    Mapping wrongOwner(L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00020004;;;AU)");
    Publish(wrongOwner);
    Require(ReadNamed(wrongOwner.name.c_str(),s)==ReadResult::Invalid,"accepted non-SYSTEM owner");
    std::cout<<"{\"untrusted_owner_rejected\":true,\"untrusted_dacl_rejected\":true}"<<std::endl;
}
void SelfTest() {
    Mapping good(PolicySDDL);Snapshot s;
    Require(TrustedMapping(good.handle.value),"native owner/DACL representation mismatch");
    Require(ReadNamed(good.name.c_str(),s)==ReadResult::Invalid,"accepted unpublished snapshot");
    Publish(good);
    Require(ReadNamed(good.name.c_str(),s)==ReadResult::Ready,"published snapshot not Ready");
    Require(s.sessionId==42 && s.enabled && std::wstring(reinterpret_cast<const wchar_t*>(s.operatorName))==L"QA-İpek ŞEN \"Destek\"","snapshot mismatch");
    PrintSnapshot(ReadResult::Ready,s,true);
    RestrictedAccess(good.name.c_str());
    Put64(static_cast<unsigned char*>(good.view)+24,Now()-1);
    Require(ReadNamed(good.name.c_str(),s)==ReadResult::Invalid,"expired snapshot accepted");
    Publish(good);
    InterlockedExchange(static_cast<volatile LONG*>(good.view),0);
    Require(ReadNamed(good.name.c_str(),s)==ReadResult::Invalid,"invalidated snapshot accepted");
    TestUntrusted();
    const std::wstring missing=good.name+L".missing";
    Require(ReadNamed(missing.c_str(),s)==ReadResult::Absent,"absent snapshot mismatch");
    std::cout<<"{\"self_test\":\"PASS\"}"<<std::endl;
}
} // namespace

int wmain(int argc,wchar_t** argv) {
    FILE* report=nullptr;
    if(argc==4 && std::wstring(argv[2])==L"--report") {
        if(_wfreopen_s(&report,argv[3],L"w",stdout)!=0) return 2;
    }
    try {
        if(argc>=2 && std::wstring(argv[1])==L"--self-test") SelfTest();
        else if(argc>=2 && std::wstring(argv[1])==L"--test-untrusted") TestUntrusted();
        else if(argc==2 && std::wstring(argv[1])==L"--check-access") CheckAccess(ProductionName);
        else if(argc>=3 && std::wstring(argv[1])==L"--read") {
            Snapshot s;const auto state=Read(s);
            PrintSnapshot(state,s,argc==4 && std::wstring(argv[3])==L"--show-test-operator");
            const wchar_t* actual=state==ReadResult::Ready?L"Ready":state==ReadResult::Absent?L"Absent":L"Invalid";
            Require(std::wstring(argv[2])==actual,"unexpected snapshot state");
        } else throw std::runtime_error("usage: --self-test [--report path] | --test-untrusted | --read Ready|Absent|Invalid [--show-test-operator] | --check-access (standard user)");
        return 0;
    } catch(const std::exception& error) {
        std::cout<<"{\"error\":\""<<error.what()<<"\"}"<<std::endl;
        return 1;
    }
}
