#pragma once
// Wii-format Super Mario Galaxy saves (GameData.bin) in the native port.
//
// The native game reads and writes GameData.bin in the host's little-endian
// order; a Wii save holds the same layout big-endian. convert() swaps every
// multi-byte field between the two, using the field widths of the game's own
// serializers, and rebuilds the header, index and checksum
// (native/tools/wii_save_to_native.py is the same conversion as a script).
//
// importOnce() is GalaxyPad's save copy: the first time its New engine starts,
// the Original engine's save is copied in. It never writes the Wii save.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace PetariNative::App::WiiSave {

enum class Order { Wii, Native };  // big-endian (Wii) or little-endian (native)

// Converts `data` from `from` to the other order. False, with `error` set and
// `data` unspecified, for a file that does not have the expected structure.
bool convert(std::vector<std::uint8_t>& data, Order from, std::string* error);

// If <user>/NAND/title/00010000/524d4745/data/GameData.bin does not exist yet and
// `wiiSave` does: keeps a byte-checked copy of `wiiSave` under
// <user>/wii-save-import/, converts it, converts the result back and requires the
// original bytes, then writes the native save. Otherwise does nothing. Logs the
// outcome; never fails the launch (without a save the game starts fresh).
void importOnce(const std::filesystem::path& wiiSave, const std::filesystem::path& user);

}  // namespace PetariNative::App::WiiSave
