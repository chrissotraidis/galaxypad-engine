// seed_pack.hpp's expansion as a command, for native/tests/seed_pack_native_tests.py:
//   petari_seed_pack_tool expand|ensure PACK OUT_DIR
#include "../app/seed_pack.hpp"

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    if (argc != 4 || (std::strcmp(argv[1], "expand") != 0 && std::strcmp(argv[1], "ensure") != 0)) {
        std::fputs("usage: petari_seed_pack_tool expand|ensure PACK OUT_DIR\n", stderr);
        return 2;
    }
    namespace SeedPack = PetariNative::App::SeedPack;
    std::string error;
    const bool ok = std::strcmp(argv[1], "expand") == 0 ? SeedPack::expand(argv[2], argv[3], &error)
                                                        : !SeedPack::ensure(argv[2], argv[3], &error).empty();
    if (!ok) std::fprintf(stderr, "%s\n", error.c_str());
    return ok ? 0 : 1;
}
