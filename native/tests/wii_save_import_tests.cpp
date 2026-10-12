// WiiSave::convert and GalaxyPad's save sync (native/app/wii_save_import.hpp) on a
// small synthetic save.
#include "../app/wii_save_import.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unistd.h>

namespace {
using Bytes = std::vector<std::uint8_t>;
using PetariNative::App::WiiSave::Order;
using PetariNative::App::WiiSave::Sync;
using PetariNative::App::WiiSave::convert;
namespace fs = std::filesystem;
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}
void be16(Bytes& b, unsigned v) { b.push_back(v >> 8); b.push_back(v); }
void be32(Bytes& b, unsigned v) { be16(b, v >> 16); be16(b, v & 0xFFFF); }
unsigned le16(const Bytes& b, size_t o) { return b[o] | b[o + 1] << 8; }
unsigned le32(const Bytes& b, size_t o) { return le16(b, o) | le16(b, o + 2) << 16; }
void put32be(Bytes& b, size_t o, unsigned v) { for (int i = 0; i < 4; i++) b[o + i] = v >> (24 - 8 * i); }

// One "GameData" holder with a PLAY and a GALA chunk, Wii (big-endian) order.
// `progress` stands in for play: it is the first PLAY byte.
Bytes wiiSave(unsigned progress = 0x7F) {
    Bytes b(32, 0);
    std::memcpy(&b[16], "GameData", 8);
    b.push_back(1); b.push_back(2); b.push_back(0); b.push_back(0);
    be32(b, 0x504C4159); be32(b, 0x1111); be32(b, 12 + 7);  // PLAY
    b.push_back(progress); be32(b, 0x01020304); be16(b, 0x0506);
    be32(b, 0x47414C41); be32(b, 0x2222); be32(b, 12 + 2 + 20 + 20);  // GALA
    be16(b, 1);                                      // galaxies
    be16(b, 4); be16(b, 20);                         // content header: count, record size
    be16(b, 0xA); be16(b, 0); be16(b, 0xB); be16(b, 2); be16(b, 0xC); be16(b, 3); be16(b, 0xD); be16(b, 4);
    be16(b, 0xBEEF); b.push_back(1); b.push_back(0);  // name, star flag, first play
    for (unsigned i = 0; i < 8; i++) be16(b, 0x100 + i);  // max coins
    put32be(b, 4, 2);                  // version
    put32be(b, 8, 1);                  // index entries
    put32be(b, 12, b.size());          // size
    put32be(b, 28, 32);                // GameData offset
    unsigned total = 0, inverse = 0;
    for (size_t o = 4; o + 1 < b.size(); o += 2) {
        const unsigned w = b[o] << 8 | b[o + 1];
        total = (total + w) & 0xFFFF;
        inverse = (inverse + (~w & 0xFFFF)) & 0xFFFF;
    }
    put32be(b, 0, total << 16 | inverse);
    return b;
}

Bytes nativeSave(unsigned progress) {
    Bytes b = wiiSave(progress);
    std::string error;
    convert(b, Order::Wii, &error);
    return b;
}

Bytes read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write(const fs::path& path, const Bytes& data) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), data.size());
}

size_t kept(const fs::path& user) {
    std::error_code ec;
    size_t n = 0;
    for (auto it = fs::directory_iterator(user / "wii-save-sync", ec); !ec && it != fs::directory_iterator(); ++it) n++;
    return n;
}

// GalaxyPad's engines take turns: progress in either reaches the other, and a
// save that changed since the last copy is never overwritten.
void testSync() {
    using namespace PetariNative::App::WiiSave;
    const fs::path root = fs::temp_directory_path() / ("petari-wii-sync-" + std::to_string(::getpid()));
    fs::remove_all(root);
    const fs::path wii = root / "Wii/title/00010000/524d4745/data/GameData.bin";
    const fs::path user = root / "NewEngine";
    const fs::path native = user / "NAND/title/00010000/524d4745/data/GameData.bin";

    check(importFromWii(wii, user) == Sync::UpToDate && !fs::exists(native), "no Wii save: nothing to copy");
    write(wii, wiiSave(1));
    check(importFromWii(wii, user) == Sync::Copied && read(native) == nativeSave(1), "first New launch copies the Wii save in");
    check(read(wii) == wiiSave(1) && kept(user) == 0, "the Wii save is only read; nothing replaced");
    check(importFromWii(wii, user) == Sync::UpToDate, "nothing new: no copy");

    write(native, nativeSave(2));  // played in New
    check(importFromWii(wii, user) == Sync::UpToDate && read(native) == nativeSave(2), "New's progress is not undone at start");
    check(exportToWii(wii, user) == Sync::Copied && read(wii) == wiiSave(2), "switching to Original copies New's progress out");
    check(kept(user) == 1, "the replaced Wii save is kept");
    check(exportToWii(wii, user) == Sync::UpToDate, "switching again: no copy");

    write(wii, wiiSave(3));  // played in Original
    check(importFromWii(wii, user) == Sync::Copied && read(native) == nativeSave(3), "Original's progress reaches New");
    check(kept(user) == 2, "the replaced native save is kept");

    write(wii, wiiSave(4));    // Original progressed...
    write(native, nativeSave(5));  // ...and so did New, without a copy between
    check(importFromWii(wii, user) == Sync::Conflict && read(native) == nativeSave(5), "both changed: New keeps its save");
    check(exportToWii(wii, user) == Sync::Conflict && read(wii) == wiiSave(4), "both changed: Original keeps its save");

    write(wii, Bytes(60, 0));
    check(importFromWii(wii, user) == Sync::Failed && read(native) == nativeSave(5), "a damaged Wii save changes nothing");
    fs::remove_all(root);
}
}  // namespace

int main() {
    const Bytes original = wiiSave();
    Bytes native = original;
    std::string error;
    check(convert(native, Order::Wii, &error), "Wii save converts");
    check(le32(native, 8) == 1 && le32(native, 12) == original.size() && le32(native, 28) == 32, "header and index swapped");
    check(le32(native, 32 + 4) == 0x504C4159 && le32(native, 32 + 12) == 19, "chunk header swapped");
    check(native[32 + 16] == 0x7F && le32(native, 32 + 17) == 0x01020304 && le16(native, 32 + 21) == 0x0506, "PLAY fields");
    const size_t gala = 32 + 4 + 19 + 12;
    check(le16(native, gala) == 1 && le16(native, gala + 2) == 4 && le16(native, gala + 4) == 20, "GALA counts");
    check(le16(native, gala + 2 + 20) == 0xBEEF && le16(native, gala + 2 + 20 + 4 + 14) == 0x107, "GALA record");
    unsigned total = 0, inverse = 0;
    for (size_t o = 4; o + 1 < native.size(); o += 2) {
        total = (total + le16(native, o)) & 0xFFFF;
        inverse = (inverse + (~le16(native, o) & 0xFFFF)) & 0xFFFF;
    }
    check(le32(native, 0) == (total << 16 | inverse), "native checksum");
    Bytes back = native;
    check(convert(back, Order::Native, &error) && back == original, "converting back gives the original bytes");
    Bytes truncated(original.begin(), original.begin() + 60);
    check(!convert(truncated, Order::Wii, &error), "a truncated save is rejected");
    Bytes unknown = original;
    unknown[32 + 4] = 'X';
    check(!convert(unknown, Order::Wii, &error), "an unknown chunk is rejected");
    testSync();
    if (failures == 0) std::puts("Wii save conversion and sync tests passed");
    return failures == 0 ? 0 : 1;
}
