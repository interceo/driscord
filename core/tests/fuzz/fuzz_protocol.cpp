// Fuzzes the shared signaling wire format: parse -> encode -> parse.
//
// Anything the parser accepts must survive its own encoder — a message that
// round-trips into a parse error means the two halves of the protocol have
// drifted, which is exactly the bug class that shows up as "the server
// understood the client but not itself".

#include "signaling_protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const std::string_view raw(reinterpret_cast<const char*>(data), size);
    auto parsed = signaling::parse(raw);
    if (!parsed) {
        return 0;
    }
    const std::string encoded = signaling::dump(parsed.value());
    auto reparsed = signaling::parse(encoded);
    if (!reparsed) {
        std::fprintf(stderr,
            "protocol roundtrip drift: encoder output no longer parses\n");
        std::abort();
    }
    return 0;
}
