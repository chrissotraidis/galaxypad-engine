#pragma once
// Wii-format Super Mario Galaxy saves (GameData.bin) in the native port.
//
// The native game reads and writes GameData.bin in the host's little-endian
// order; a Wii save holds the same layout big-endian. convert() swaps every
// multi-byte field between the two, using the field widths of the game's own
// serializers, and rebuilds the header, index and checksum
// (native/tools/wii_save_to_native.py is the same conversion as a script).
//
// importFromWii() and exportToWii() keep GalaxyPad's two engines on the same
// progress. The Original engine keeps it in a Wii save, the New engine in
// <user>/NAND/title/00010000/524d4745/data/GameData.bin. After each copy,
// <user>/wii-save-sync.txt records a checksum of both files. A copy replaces a
// save only when it is missing or unchanged since the last copy, so progress made
// in either engine is never overwritten; when both changed, each keeps its own.
// Every replaced file is kept, byte-checked, under <user>/wii-save-sync/, and
// each copy must convert back to the bytes it came from.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace PetariNative::App::WiiSave {

enum class Order { Wii, Native };  // big-endian (Wii) or little-endian (native)

// Converts `data` from `from` to the other order. False, with `error` set and
// `data` unspecified, for a file that does not have the expected structure.
bool convert(std::vector<std::uint8_t>& data, Order from, std::string* error);

enum class Sync { Copied, UpToDate, Conflict, Failed };

// When the New engine starts: the Wii save into the native save.
Sync importFromWii(const std::filesystem::path& wiiSave, const std::filesystem::path& user);
// When the player switches to the Original engine: the native save into the Wii save.
Sync exportToWii(const std::filesystem::path& wiiSave, const std::filesystem::path& user);

}  // namespace PetariNative::App::WiiSave
