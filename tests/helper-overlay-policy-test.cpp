// Portable wire-contract test; does NOT claim Win32 ACL/runtime acceptance.
#include "../winvnc/winvnc/HelperOverlayPolicy.h"
#include <cassert>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    using namespace HelperOverlayPolicy;
    unsigned char wire[WireSize] = {};
    const unsigned char header[] = {'A','C','H','O','V','L','1',0,1,0,0,0,64,2,0,0};
    std::memcpy(wire, header, sizeof(header));
    wire[16] = 42;
    wire[24] = 100;
    wire[32] = 1;
    wire[36] = 1;
    wire[40] = 0x30; wire[41] = 0x01; // U+0130 capital dotted I
    Snapshot s;
    assert(Parse(wire, sizeof(wire), 99, s));
    assert(s.sessionId == 42 && s.enabled && s.operatorName[0] == 0x0130);
    assert(!Parse(wire, sizeof(wire), 100, s));
    wire[32] = 0;
    assert(!Parse(wire, sizeof(wire), 99, s));
    wire[32] = 1;
    wire[40] = 0; wire[41] = 0xd8;
    assert(!Parse(wire, sizeof(wire), 99, s));
    // Optional binary emitted by the Go encoder for cross-language validation.
    if (argc == 2) {
        std::ifstream input(argv[1], std::ios::binary);
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
        assert(Parse(bytes.data(), bytes.size(), 1800000000, s));
        assert(s.sessionId == 42 && s.operatorName[0] == 0x0130);
        const std::u16string expected = u"İpek ŞEN \"Destek\" 😀";
        for (std::size_t i = 0; i < expected.size(); ++i)
            assert(s.operatorName[i] == expected[i]);
        assert(s.operatorName[expected.size()] == 0);
    }
}
