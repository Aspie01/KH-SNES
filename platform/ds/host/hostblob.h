#pragma once
// Reading a scene's data off disk, for the host tier only.
//
// This lives in host/ and not in source/ on purpose.  On the device a scene's
// tables are linked into the binary and a Blob points at a symbol; there is no
// filesystem and no reason to want one.  The two answers to "where do the bytes
// come from" are the whole of what §M2 has to decide, and keeping the host's
// answer out of the shared tree is what stops that decision leaking early.
//
// KH_ASSET_DIR comes from Makefile.host as an absolute path, so a test does not
// depend on the working directory the suite is run from.

#include <cstdio>
#include <cstring>

#include "scene.h"

namespace khhost {

constexpr size_t MAX_BLOB = 64 * 1024;

// Loads assets/gen/ds/<name> into a caller-owned buffer.  Returns an empty Blob
// if it is not there -- which means the pipeline has not been run, and the
// caller should say so rather than quietly passing.
inline kh::Blob load(const char* name, unsigned char* buf, size_t cap) {
    char path[512];
    std::snprintf(path, sizeof path, "%s/%s", KH_ASSET_DIR, name);
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return {};
    const size_t n = std::fread(buf, 1, cap, f);
    std::fclose(f);
    return kh::Blob{buf, n};
}

// ...and the SNES artefacts, one directory up, for the one test that compares
// the two encoders against each other on art both machines paint.
inline kh::Blob loadSnes(const char* name, unsigned char* buf, size_t cap) {
    char path[512];
    std::snprintf(path, sizeof path, "%s/../%s", KH_ASSET_DIR, name);
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return {};
    const size_t n = std::fread(buf, 1, cap, f);
    std::fclose(f);
    return kh::Blob{buf, n};
}

}  // namespace khhost
