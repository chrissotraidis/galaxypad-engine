#pragma once
// Pipeline seed packs (native/tools/seedpack.py): every stage's Aurora pipeline
// manifest packed losslessly into a few hundred KB of xz. Shipping the pack
// instead of the expanded databases keeps about 400 MB out of an iOS app; the
// engine expands it once into a cache folder and points PETARI_PIPELINE_SEED_DIR
// there (native/tools/PIPELINE_SEEDING.md).

#include <filesystem>
#include <string>

namespace PetariNative::App::SeedPack {

// Writes __global__.db and every <Stage>.db of the pack into outDir, with the same
// rows as seedpack.py expand. Writes into a temporary sibling folder and renames
// it into place, so outDir is either the full expansion or absent.
bool expand(const std::filesystem::path& pack, const std::filesystem::path& outDir, std::string* error);

// Returns outDir after expanding the pack into it if outDir does not already hold
// this pack's expansion (a marker records the pack's size and checksum); empty
// on failure, with error set.
std::filesystem::path ensure(const std::filesystem::path& pack, const std::filesystem::path& outDir,
                             std::string* error);

}  // namespace PetariNative::App::SeedPack
