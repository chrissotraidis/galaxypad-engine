// Pipeline seed packs: see seed_pack.hpp.
#include "seed_pack.hpp"

#include <sqlite3.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

#if defined(__APPLE__)
#include <compression.h>
#endif

namespace PetariNative::App::SeedPack {
namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;

constexpr std::uint32_t kNever = 0xFFFFFFFFu;
constexpr const char* kMarker = ".seedpack-native";
constexpr const char* kSchema =
    "CREATE TABLE aurora_schema(value INTEGER);"
    "CREATE TABLE pipeline_cache(type INTEGER, hash INTEGER, config_version INTEGER, config_size INTEGER, "
    "config BLOB, first_frame_used INTEGER, PRIMARY KEY(type,hash));"
    "CREATE INDEX pipeline_cache_load_order_idx ON pipeline_cache(first_frame_used);"
    "INSERT INTO aurora_schema VALUES(1);";

struct Config {
    std::uint8_t type;
    std::int64_t hash;
    std::uint16_t version;
    std::uint16_t size;
    std::uint32_t firstFrame;
    const std::uint8_t* data;
};

struct Stage {
    std::string name;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> rows;  // config index, first frame
};

bool readFile(const fs::path& path, Bytes* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

bool decompress(const Bytes& packed, Bytes* out, std::string* error) {
#if defined(__APPLE__)
    compression_stream stream;
    if (compression_stream_init(&stream, COMPRESSION_STREAM_DECODE, COMPRESSION_LZMA) != COMPRESSION_STATUS_OK) {
        *error = "cannot start xz decoding";
        return false;
    }
    std::uint8_t chunk[1 << 16];
    stream.src_ptr = packed.data();
    stream.src_size = packed.size();
    compression_status status;
    do {
        stream.dst_ptr = chunk;
        stream.dst_size = sizeof(chunk);
        status = compression_stream_process(&stream, COMPRESSION_STREAM_FINALIZE);
        out->insert(out->end(), chunk, chunk + (sizeof(chunk) - stream.dst_size));
        if (out->size() > (256u << 20)) status = COMPRESSION_STATUS_ERROR;
    } while (status == COMPRESSION_STATUS_OK);
    compression_stream_destroy(&stream);
    if (status != COMPRESSION_STATUS_END) {
        *error = "the pack is not valid xz";
        return false;
    }
    return true;
#else
    (void)packed;
    (void)out;
    *error = "seed packs are expanded natively on Apple platforms only";
    return false;
#endif
}

struct Reader {
    const Bytes& buf;
    std::size_t at = 0;
    bool ok = true;
    bool need(std::size_t n) {
        if (!ok || n > buf.size() - at) ok = false;
        return ok;
    }
    std::uint64_t le(std::size_t n) {
        if (!need(n)) return 0;
        std::uint64_t v = 0;
        for (std::size_t i = 0; i < n; i++) v |= static_cast<std::uint64_t>(buf[at + i]) << (8 * i);
        at += n;
        return v;
    }
    std::uint64_t varint() {
        std::uint64_t v = 0;
        for (int shift = 0; ok && shift < 64; shift += 7) {
            const std::uint64_t byte = le(1);
            v |= (byte & 0x7F) << shift;
            if (!(byte & 0x80)) return v;
        }
        ok = false;
        return 0;
    }
};

bool parse(const Bytes& buf, std::vector<Config>* configs, std::vector<Stage>* stages, std::string* error) {
    Reader r{buf};
    if (!r.need(4) || std::memcmp(buf.data(), "PSD1", 4) != 0) {
        *error = "not a seed pack";
        return false;
    }
    r.at = 4;
    const std::uint64_t count = r.le(4);
    if (!r.need(count * 17)) {
        *error = "truncated config table";
        return false;
    }
    configs->resize(count);
    for (Config& c : *configs) {
        c.type = static_cast<std::uint8_t>(r.le(1));
        c.hash = static_cast<std::int64_t>(r.le(8));
        c.version = static_cast<std::uint16_t>(r.le(2));
        c.size = static_cast<std::uint16_t>(r.le(2));
        c.firstFrame = static_cast<std::uint32_t>(r.le(4));
    }
    for (Config& c : *configs) {
        if (!r.need(c.size)) break;
        c.data = buf.data() + r.at;
        r.at += c.size;
    }
    const std::uint64_t stageCount = r.le(4);
    for (std::uint64_t s = 0; r.ok && s < stageCount; s++) {
        Stage stage;
        const std::size_t length = static_cast<std::size_t>(r.le(1));
        if (!r.need(length)) break;
        stage.name.assign(reinterpret_cast<const char*>(buf.data() + r.at), length);
        r.at += length;
        for (char ch : stage.name) {
            if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '.' || ch == '-')) r.ok = false;
        }
        if (stage.name.empty() || stage.name == "__global__") r.ok = false;
        const std::uint64_t rows = r.varint();
        for (std::uint64_t i = 0; r.ok && i < rows; i++) {
            const std::uint64_t index = r.varint();
            const std::uint64_t frame = r.varint();
            if (index >= count || frame > kNever) r.ok = false;
            stage.rows.emplace_back(static_cast<std::uint32_t>(index),
                                    frame == 0 ? kNever : static_cast<std::uint32_t>(frame - 1));
        }
        stages->push_back(std::move(stage));
    }
    if (!r.ok || r.at != buf.size()) {
        *error = "the pack is damaged";
        return false;
    }
    return true;
}

bool writeDatabase(const fs::path& path, const std::vector<Config>& configs,
                   const std::vector<std::pair<std::uint32_t, std::uint32_t>>& rows, std::string* error) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        *error = "cannot create " + path.string();
        sqlite3_close(db);
        return false;
    }
    bool ok = sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr) == SQLITE_OK &&
              sqlite3_exec(db, kSchema, nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_stmt* insert = nullptr;
    ok = ok && sqlite3_prepare_v2(db, "INSERT INTO pipeline_cache VALUES(?,?,?,?,?,?)", -1, &insert, nullptr) == SQLITE_OK;
    for (std::size_t i = 0; ok && i < rows.size(); i++) {
        const Config& c = configs[rows[i].first];
        sqlite3_bind_int(insert, 1, c.type);
        sqlite3_bind_int64(insert, 2, c.hash);
        sqlite3_bind_int(insert, 3, c.version);
        sqlite3_bind_int(insert, 4, c.size);
        sqlite3_bind_blob(insert, 5, c.data, c.size, SQLITE_STATIC);
        sqlite3_bind_int64(insert, 6, rows[i].second);
        ok = sqlite3_step(insert) == SQLITE_DONE;
        sqlite3_reset(insert);
    }
    sqlite3_finalize(insert);
    ok = ok && sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
    if (!ok) *error = "cannot write " + path.string() + ": " + sqlite3_errmsg(db);
    sqlite3_close(db);
    return ok;
}

// Change detection for the marker (FNV-1a, 64 bits), with the pack's size.
std::string identity(const Bytes& packed) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::uint8_t b : packed) h = (h ^ b) * 0x100000001b3ull;
    char text[64];
    std::snprintf(text, sizeof(text), "%zu %016llx", packed.size(), static_cast<unsigned long long>(h));
    return text;
}

bool expandBytes(const Bytes& packed, const fs::path& outDir, std::string* error) {
    Bytes buf;
    std::vector<Config> configs;
    std::vector<Stage> stages;
    if (!decompress(packed, &buf, error) || !parse(buf, &configs, &stages, error)) return false;
    std::error_code ec;
    const fs::path partial = outDir.parent_path() / (outDir.filename().string() + ".partial");
    fs::remove_all(partial, ec);
    if (!fs::create_directories(partial, ec) || ec) {
        *error = "cannot create " + partial.string();
        return false;
    }
    std::vector<std::pair<std::uint32_t, std::uint32_t>> all;
    for (std::uint32_t i = 0; i < configs.size(); i++) all.emplace_back(i, configs[i].firstFrame);
    bool ok = writeDatabase(partial / "__global__.db", configs, all, error);
    for (std::size_t s = 0; ok && s < stages.size(); s++) {
        ok = writeDatabase(partial / (stages[s].name + ".db"), configs, stages[s].rows, error);
    }
    if (ok) {
        std::ofstream marker(partial / kMarker, std::ios::trunc);
        marker << identity(packed) << "\n";
        ok = static_cast<bool>(marker);
    }
    if (ok) {
        fs::remove_all(outDir, ec);
        fs::rename(partial, outDir, ec);
        ok = !ec;
        if (!ok) *error = "cannot move the seeds into " + outDir.string();
    }
    if (!ok) fs::remove_all(partial, ec);
    return ok;
}

}  // namespace

bool expand(const fs::path& pack, const fs::path& outDir, std::string* error) {
    Bytes packed;
    if (!readFile(pack, &packed)) {
        *error = "cannot read " + pack.string();
        return false;
    }
    return expandBytes(packed, outDir, error);
}

fs::path ensure(const fs::path& pack, const fs::path& outDir, std::string* error) {
    Bytes packed;
    if (!readFile(pack, &packed)) {
        *error = "cannot read " + pack.string();
        return {};
    }
    std::ifstream marker(outDir / kMarker);
    std::string recorded;
    std::getline(marker, recorded);
    std::error_code ec;
    if (recorded == identity(packed) && fs::is_regular_file(outDir / "__global__.db", ec)) return outDir;
    if (!expandBytes(packed, outDir, error)) return {};
    return outDir;
}

}  // namespace PetariNative::App::SeedPack
