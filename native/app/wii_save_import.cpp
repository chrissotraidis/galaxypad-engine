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

namespace {

// Change detection only (FNV-1a, 64 bits); "absent" for a missing file.
std::string checksum(const Bytes* data) {
    if (data == nullptr) return "absent";
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::uint8_t b : *data) h = (h ^ b) * 0x100000001b3ull;
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(h));
    return text;
}

struct Record {
    bool present = false;
    std::string wii, native;
};

Record readRecord(const fs::path& user) {
    Record r;
    std::ifstream in(user / "wii-save-sync.txt");
    std::string key, value;
    while (in >> key >> value) {
        if (key == "wii") r.wii = value;
        if (key == "native") r.native = value;
    }
    r.present = !r.wii.empty() && !r.native.empty();
    return r;
}

bool writeRecord(const fs::path& user, const std::string& wii, const std::string& native) {
    const fs::path path = user / "wii-save-sync.txt", partial = user / "wii-save-sync.txt.partial";
    {
        std::ofstream out(partial, std::ios::trunc);
        out << "wii " << wii << "\nnative " << native << "\n";
        if (!out) return false;
    }
    std::error_code ec;
    fs::rename(partial, path, ec);
    return !ec;
}

// Reads `path` into `out`; false when it does not exist. Sets `failed` on a read error.
bool readIfPresent(const fs::path& path, Bytes* out, bool* failed) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        *failed = static_cast<bool>(ec);
        return false;
    }
    *failed = !readFile(path, out);
    return !*failed;
}

// Keeps a byte-checked copy of `data` under <user>/wii-save-sync/.
bool keep(const fs::path& user, const char* label, const Bytes& data) {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::gmtime(&now));
    const fs::path folder = user / "wii-save-sync";
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) return false;
    for (int n = 0; n < 1000; n++) {
        const fs::path path = folder / (std::string("GameData-") + label + "-" + stamp + (n ? "-" + std::to_string(n) : "") + ".bin");
        if (!fs::exists(path, ec) && !ec) return writeFile(path, data);
    }
    return false;
}

// Writes `data` next to `dest`, checks it, then puts it in place.
bool place(const fs::path& dest, const Bytes& data) {
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    const fs::path partial = dest.parent_path() / (dest.filename().string() + ".sync");
    if (ec || !writeFile(partial, data)) {
        fs::remove(partial, ec);
        return false;
    }
    fs::rename(partial, dest, ec);
    return !ec;
}

fs::path nativeSave(const fs::path& user) {
    return user / "NAND/title/00010000/524d4745/data/GameData.bin";
}

// One direction: `source` (in order `from`) replaces `dest` when `dest` is missing
// or unchanged since the last copy. `importing` is true for Wii -> native.
Sync copySave(const fs::path& sourcePath, Order from, const fs::path& destPath, const fs::path& user, bool importing) {
    const char* what = importing ? "save import" : "save export";
    bool failed = false;
    Bytes source, dest;
    if (!readIfPresent(sourcePath, &source, &failed)) {
        if (failed) std::fprintf(stderr, "petari: %s: could not read %s\n", what, sourcePath.c_str());
        return failed ? Sync::Failed : Sync::UpToDate;
    }
    const bool hasDest = readIfPresent(destPath, &dest, &failed);
    if (failed) {
        std::fprintf(stderr, "petari: %s: could not read %s\n", what, destPath.c_str());
        return Sync::Failed;
    }
    Bytes converted = source;
    std::string error;
    if (!convert(converted, from, &error)) {
        std::fprintf(stderr, "petari: %s: not a Galaxy GameData.bin (%s); nothing copied\n", what, error.c_str());
        return Sync::Failed;
    }
    Bytes roundTrip = converted;
    if (!convert(roundTrip, from == Order::Wii ? Order::Native : Order::Wii, &error) || roundTrip != source) {
        std::fprintf(stderr, "petari: %s: converting back did not give the same bytes; nothing copied\n", what);
        return Sync::Failed;
    }
    const std::string sourceSum = checksum(&source), destSum = checksum(hasDest ? &dest : nullptr);
    const Record record = readRecord(user);
    const std::string& recordedSource = importing ? record.wii : record.native;
    const std::string& recordedDest = importing ? record.native : record.wii;
    auto remember = [&](const std::string& destNow) {
        return importing ? writeRecord(user, sourceSum, destNow) : writeRecord(user, destNow, sourceSum);
    };
    if (hasDest && converted == dest) {
        remember(destSum);
        return Sync::UpToDate;
    }
    if (record.present && recordedSource == sourceSum) return Sync::UpToDate;  // no new progress here
    if (hasDest && !(record.present && recordedDest == destSum)) {
        std::fprintf(stderr, "petari: %s: both engines' saves changed since the last copy; each keeps its own\n", what);
        return Sync::Conflict;
    }
    if (hasDest && !keep(user, importing ? "native" : "wii", dest)) {
        std::fprintf(stderr, "petari: %s: could not keep a checked copy of the save it replaces; nothing copied\n", what);
        return Sync::Failed;
    }
    if (!place(destPath, converted) || !remember(checksum(&converted))) {
        std::fprintf(stderr, "petari: %s: could not write %s\n", what, destPath.c_str());
        return Sync::Failed;
    }
    std::fprintf(stderr, "petari: %s: copied (%zu bytes)\n", what, converted.size());
    return Sync::Copied;
}

}  // namespace

Sync importFromWii(const fs::path& wiiSave, const fs::path& user) {
    return copySave(wiiSave, Order::Wii, nativeSave(user), user, true);
}

Sync exportToWii(const fs::path& wiiSave, const fs::path& user) {
    return copySave(nativeSave(user), Order::Native, wiiSave, user, false);
}

}  // namespace PetariNative::App::WiiSave
