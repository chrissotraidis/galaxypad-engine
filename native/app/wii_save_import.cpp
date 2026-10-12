// Wii-format saves: see wii_save_import.hpp.
#include "wii_save_import.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

namespace PetariNative::App::WiiSave {
namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;

constexpr std::size_t kMaxSaveBytes = 1u << 20;

struct Converter {
    Bytes& buf;
    Order from;

    void need(std::size_t off, std::size_t n) const {
        if (off > buf.size() || n > buf.size() - off) throw std::runtime_error("field outside the file");
    }
    std::uint32_t read(std::size_t off, std::size_t n) const {
        need(off, n);
        std::uint32_t v = 0;
        for (std::size_t i = 0; i < n; i++) {
            const std::uint8_t b = buf[off + (from == Order::Wii ? i : n - 1 - i)];
            v = (v << 8) | b;
        }
        return v;
    }
    std::uint32_t u16(std::size_t off) const { return read(off, 2); }
    std::uint32_t u32(std::size_t off) const { return read(off, 4); }
    void swap(std::size_t off, std::size_t n) {
        need(off, n);
        std::reverse(buf.begin() + static_cast<std::ptrdiff_t>(off), buf.begin() + static_cast<std::ptrdiff_t>(off + n));
    }
    void swapU16s(std::size_t start, std::size_t end) {
        for (std::size_t o = start; o + 1 < end; o += 2) swap(o, 2);
    }

    struct Header {
        std::size_t bytes;
        std::uint32_t dataSize;
        std::vector<std::uint32_t> offsets;
    };
    // BinaryDataContentHeaderSerializer: u16 count, u16 data size, (u16 hash, u16 offset)*.
    Header contentHeader(std::size_t at) {
        const std::uint32_t count = u16(at);
        Header h{4 + 4 * static_cast<std::size_t>(count), u16(at + 2), {}};
        for (std::uint32_t i = 0; i < count; i++) h.offsets.push_back(u16(at + 4 + 4 * i + 2));
        swapU16s(at, at + h.bytes);
        return h;
    }

    void payload(const std::string& sig, std::size_t start, std::size_t end) {
        if (end < start || end > buf.size()) throw std::runtime_error("chunk outside the file");
        if (sig == "PLAY") {
            swap(start + 1, 4);
            if (end - start >= 7) swap(start + 5, 2);
        } else if (sig == "FLG1" || sig == "PCE1" || sig == "VLE1") {
            swapU16s(start, end);
        } else if (sig == "SPN1") {
            need(start, 1);
            std::size_t pos = start + 1;
            for (std::uint32_t g = 0, n = buf[start]; g < n; g++) {
                const std::uint32_t block = u16(pos + 2);
                need(pos + 4, 1);
                const std::uint32_t scenarios = buf[pos + 4];
                std::size_t at = pos + 6;
                for (std::uint32_t s = 0; s < scenarios; s++) {
                    const std::uint32_t size = u16(at);
                    swap(at, 2);
                    if (size == 0) throw std::runtime_error("empty SPN1 scenario");
                    at += size;
                }
                swap(pos, 2);
                swap(pos + 2, 2);
                if (block == 0) throw std::runtime_error("empty SPN1 galaxy");
                pos += block;
            }
        } else if (sig == "GALA") {
            const std::uint32_t galaxies = u16(start);
            swap(start, 2);
            const Header h = contentHeader(start + 2);
            if (h.offsets.size() != 4) throw std::runtime_error("GALA attribute count");
            // In the game's order: mGalaxyName (u16), mPowerStarFlag (u8),
            // mFirstPlayFlag (u8), mMaxCoinNum (8 x u16).
            const std::size_t nameOff = h.offsets[0], coinOff = h.offsets[3];
            const std::size_t records = start + 2 + h.bytes;
            for (std::uint32_t i = 0; i < galaxies; i++) {
                const std::size_t r = records + static_cast<std::size_t>(i) * h.dataSize;
                swap(r + nameOff, 2);
                swapU16s(r + coinOff, r + coinOff + 16);
            }
        } else if (sig == "CONF" || sig == "MII ") {
        } else if (sig == "MISC") {
            swap(start + 1, 8);
        } else if (sig == "SYSC") {
            const Header h = contentHeader(start);
            const std::size_t data = start + h.bytes;
            const std::size_t widths[] = {8, 8, 4};  // mTimeAnnounced, mTimeSent, mSentBytes
            for (std::size_t i = 0; i < h.offsets.size() && i < 3; i++) swap(data + h.offsets[i], widths[i]);
        } else {
            throw std::runtime_error("unknown chunk " + sig);
        }
    }

    // BinaryDataChunkHolder: u8 1, u8 count, 2 pad, then (u32 sig, u32 hash, u32 size) chunks.
    void holder(std::size_t start, std::size_t end) {
        need(start, end - start);
        if (std::all_of(buf.begin() + static_cast<std::ptrdiff_t>(start), buf.begin() + static_cast<std::ptrdiff_t>(end),
                        [](std::uint8_t b) { return b == 0; })) {
            return;
        }
        if (buf[start] != 1) throw std::runtime_error("holder header");
        std::size_t off = start + 4;
        for (std::uint32_t c = 0, n = buf[start + 1]; c < n; c++) {
            const std::uint32_t raw = u32(off), size = u32(off + 8);
            if (size < 12 || off + size > end) throw std::runtime_error("chunk size");
            const std::string sig{static_cast<char>(raw >> 24), static_cast<char>(raw >> 16),
                                  static_cast<char>(raw >> 8), static_cast<char>(raw)};
            payload(sig, off + 12, off + size);
            for (std::size_t k = 0; k < 3; k++) swap(off + 4 * k, 4);
            off += size;
        }
    }

    void run() {
        const std::uint32_t count = u32(8), size = u32(12);
        if (size > buf.size() || size < 16 || count > 64 || 16 + 16 * static_cast<std::size_t>(count) > size) {
            throw std::runtime_error("file header");
        }
        std::vector<std::uint32_t> offsets;
        for (std::uint32_t i = 0; i < count; i++) offsets.push_back(u32(16 + 16 * i + 12));
        std::vector<std::uint32_t> order = offsets;
        std::sort(order.begin(), order.end());
        for (std::size_t i = 0; i < order.size(); i++) {
            const std::uint32_t end = i + 1 < order.size() ? order[i + 1] : size;
            if (order[i] > end) throw std::runtime_error("index");
            holder(order[i], end);
        }
        for (std::uint32_t i = 0; i < count; i++) swap(16 + 16 * i + 12, 4);
        for (std::size_t k = 1; k < 4; k++) swap(4 * k, 4);
        // MR::calcCheckSum over u16 words in the new order: sum, and sum of complements.
        const bool toNative = from == Order::Wii;
        std::uint32_t total = 0, inverse = 0;
        for (std::size_t o = 4; o + 1 < size; o += 2) {
            const std::uint32_t word = toNative ? (buf[o] | buf[o + 1] << 8) : (buf[o] << 8 | buf[o + 1]);
            total = (total + word) & 0xFFFF;
            inverse = (inverse + (~word & 0xFFFF)) & 0xFFFF;
        }
        const std::uint32_t sum = total << 16 | inverse;
        for (std::size_t i = 0; i < 4; i++) {
            buf[toNative ? i : 3 - i] = static_cast<std::uint8_t>(sum >> (8 * i));
        }
    }
};

bool readFile(const fs::path& path, Bytes* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad() && out->size() <= kMaxSaveBytes;
}

bool writeFile(const fs::path& path, const Bytes& data) {
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) return false;
    }
    Bytes back;
    return readFile(path, &back) && back == data;
}

}  // namespace

bool convert(Bytes& data, Order from, std::string* error) {
    try {
        Converter{data, from}.run();
        return true;
    } catch (const std::exception& e) {
        if (error != nullptr) *error = e.what();
        return false;
    }
}

void importOnce(const fs::path& wiiSave, const fs::path& user) {
    std::error_code ec;
    const fs::path dest = user / "NAND/title/00010000/524d4745/data/GameData.bin";
    if (fs::exists(dest, ec) || ec) {
        std::fputs("petari: save import: a native save exists; the Wii save is not imported again\n", stderr);
        return;
    }
    if (!fs::is_regular_file(wiiSave, ec)) {
        std::fputs("petari: save import: no Wii save to import\n", stderr);
        return;
    }
    Bytes original;
    if (!readFile(wiiSave, &original)) {
        std::fprintf(stderr, "petari: save import: could not read %s\n", wiiSave.c_str());
        return;
    }
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::gmtime(&now));
    const fs::path backups = user / "wii-save-import";
    fs::create_directories(backups, ec);
    const fs::path backup = backups / (std::string("GameData-wii-") + stamp + ".bin");
    if (ec || fs::exists(backup) || !writeFile(backup, original)) {
        std::fputs("petari: save import: could not keep a checked copy of the Wii save; not imported\n", stderr);
        return;
    }
    Bytes converted = original;
    std::string error;
    if (!convert(converted, Order::Wii, &error)) {
        std::fprintf(stderr, "petari: save import: not a Wii GameData.bin (%s); not imported\n", error.c_str());
        return;
    }
    Bytes roundTrip = converted;
    if (!convert(roundTrip, Order::Native, &error) || roundTrip != original) {
        std::fputs("petari: save import: converting back did not give the original bytes; not imported\n", stderr);
        return;
    }
    fs::create_directories(dest.parent_path(), ec);
    const fs::path partial = dest.parent_path() / "GameData.bin.import";
    if (ec || !writeFile(partial, converted)) {
        std::fputs("petari: save import: could not write the native save\n", stderr);
        fs::remove(partial, ec);
        return;
    }
    fs::rename(partial, dest, ec);
    if (ec) {
        std::fputs("petari: save import: could not place the native save\n", stderr);
        return;
    }
    std::fprintf(stderr, "petari: save import: Wii save copied in (%zu bytes; copy kept as %s)\n", converted.size(),
                 backup.filename().c_str());
}

}  // namespace PetariNative::App::WiiSave
