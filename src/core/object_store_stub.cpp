// Object Store Implementation (backed by SHA-256 + zlib level 1)
// Based on interface in bts/object_store.h and storage primitives from src/storage/

#include "bts/object_store.h"
#include "../storage/compress.hpp"
#include "../storage/hash.hpp"

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace bts {

// ─── Repository path helpers ─────────────────────────────────────────────────

std::optional<fs::path> find_repo_root(const fs::path& start) {
    fs::path current = fs::absolute(start);
    while (true) {
        if (fs::is_directory(current / ".bts")) {
            return current;
        }
        fs::path parent = current.parent_path();
        if (parent == current) {
            break;  // reached filesystem root
        }
        current = parent;
    }
    return std::nullopt;
}

fs::path bts_dir(const fs::path& repo_root) {
    return repo_root / ".bts";
}

// ─── Init command ────────────────────────────────────────────────────────────

int cmd_init(const fs::path& path) {
    fs::path bts = fs::absolute(path) / ".bts";
    if (fs::exists(bts)) {
        std::cerr << "error: repository already initialised at " << bts << "\n";
        return 1;
    }

    fs::create_directories(bts / "objects");
    fs::create_directories(bts / "refs" / "heads");

    // Create empty index file
    {
        std::ofstream idx(bts / "index");
        // intentionally empty
    }

    // Create HEAD pointing to refs/heads/main
    {
        std::ofstream head(bts / "HEAD");
        head << "ref: refs/heads/main\n";
    }

    // Create empty config
    {
        std::ofstream cfg(bts / "config");
        // intentionally empty
    }

    std::cout << "Initialised empty bts repository in " << bts << "\n";
    return 0;
}

// ─── Config helpers ──────────────────────────────────────────────────────────

static const std::vector<std::string> allowed_keys = {"user.name", "user.email"};

static bool is_valid_config_key(const std::string& key) {
    return std::find(allowed_keys.begin(), allowed_keys.end(), key)
           != allowed_keys.end();
}

static std::map<std::string, std::string> load_config(const fs::path& repo) {
    std::map<std::string, std::string> cfg;
    fs::path config_path = bts_dir(repo) / "config";
    if (!fs::exists(config_path)) return cfg;

    std::ifstream in(config_path);
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq != std::string::npos) {
            cfg[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    return cfg;
}

static void save_config(const fs::path& repo,
                        const std::map<std::string, std::string>& cfg) {
    fs::path config_path = bts_dir(repo) / "config";
    std::ofstream out(config_path, std::ios::trunc);
    for (auto& [k, v] : cfg) {
        out << k << "=" << v << "\n";
    }
}

std::optional<std::string> get_config(const fs::path& repo,
                                      const std::string& key) {
    auto cfg = load_config(repo);
    auto it = cfg.find(key);
    if (it != cfg.end()) return it->second;
    return std::nullopt;
}

int cmd_config(const fs::path& repo, const std::string& key,
               const std::string& value, bool set_mode) {
    if (!is_valid_config_key(key)) {
        std::cerr << "error: unsupported config key '" << key
                  << "'. Supported keys: user.name, user.email\n";
        return 2;
    }

    if (set_mode) {
        auto cfg = load_config(repo);
        cfg[key] = value;
        save_config(repo, cfg);
        return 0;
    } else {
        auto val = get_config(repo, key);
        if (!val) {
            std::cerr << "error: config key '" << key << "' is not set\n";
            return 1;
        }
        std::cout << *val << "\n";
        return 0;
    }
}

// ─── Hashing (OpenSSL SHA-256) ──────────────────────────────────────────────

std::string hash_content(const std::string& data) {
    return sha256(data);
}

// ─── Object read/write (zlib level 1 + SHA-256 integrity) ───────────────────

static fs::path object_path(const fs::path& repo, const std::string& hash) {
    return bts_dir(repo) / "objects" / hash.substr(0, 2) / hash.substr(2);
}

std::string write_object(const fs::path& repo, ObjectType type,
                         const std::string& data) {
    // Build serialized content: "type size\0data"
    std::string header = object_type_string(type) + " "
                         + std::to_string(data.size());
    std::string store_data;
    store_data.reserve(header.size() + 1 + data.size());
    store_data += header;
    store_data += '\0';
    store_data += data;

    std::string hash = sha256(store_data);
    fs::path path = object_path(repo, hash);

    std::error_code ec;
    if (fs::exists(path, ec)) {
        return hash;  // already stored (SR-1, REQ-ADD-1)
    }

    fs::path parent = path.parent_path();
    fs::create_directories(parent, ec);

    std::string compressed = compress_data(store_data, 1);

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<uint32_t> dist;
    fs::path temp_path = parent / (".tmp_" + std::to_string(::getpid()) + "_" +
                                   std::to_string(dist(rng)) + "_" + hash.substr(2, 8));

    FILE* fp = std::fopen(temp_path.c_str(), "wb");
    if (!fp) {
        throw std::runtime_error("Failed to create temporary object file: " + temp_path.string());
    }
    std::size_t written = std::fwrite(compressed.data(), 1, compressed.size(), fp);
    if (written != compressed.size() || std::fflush(fp) != 0 || ::fsync(fileno(fp)) != 0) {
        std::fclose(fp);
        fs::remove(temp_path, ec);
        throw std::runtime_error("Failed to write object file: " + temp_path.string());
    }
    std::fclose(fp);

    fs::permissions(temp_path,
                    fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read,
                    fs::perm_options::replace, ec);
    fs::rename(temp_path, path, ec);
    if (ec) {
        if (!fs::exists(path, ec)) {
            fs::remove(temp_path, ec);
            throw std::runtime_error("Failed to finalize object file: " + ec.message());
        }
        fs::remove(temp_path, ec);
    }

    return hash;
}

std::optional<std::string> read_object(const fs::path& repo,
                                       const std::string& hash) {
    auto raw = read_object_raw(repo, hash);
    if (!raw.has_value()) return std::nullopt;

    auto nul = raw->find('\0');
    if (nul == std::string::npos) {
        throw IntegrityException(hash, "", "fatal: object " + hash + " has malformed header");
    }

    return raw->substr(nul + 1);
}

std::optional<std::string> read_object_raw(const fs::path& repo,
                                           const std::string& hash) {
    if (!is_valid_sha256_hex(hash)) return std::nullopt;

    fs::path path = object_path(repo, hash);
    std::error_code ec;
    if (!fs::exists(path, ec)) return std::nullopt;

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return std::nullopt;
    std::string compressed((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());

    std::string decompressed;
    try {
        decompressed = decompress_data(compressed);
    } catch (const std::exception& e) {
        throw IntegrityException(
            hash, "",
            "fatal: integrity error: object " + hash + " is corrupt (" + e.what() + ")");
    }

    std::string actual_hash = sha256(decompressed);
    if (actual_hash != hash) {
        throw IntegrityException(hash, actual_hash);
    }

    return decompressed;
}

// ─── Parse commit ───────────────────────────────────────────────────────────

CommitData parse_commit(const std::string& raw) {
    CommitData cd;
    std::istringstream iss(raw);
    std::string line;
    bool in_message = false;
    std::ostringstream msg;

    while (std::getline(iss, line)) {
        if (in_message) {
            if (msg.tellp() > 0) msg << "\n";
            msg << line;
            continue;
        }

        if (line.empty()) {
            in_message = true;
            continue;
        }

        if (line.rfind("tree ", 0) == 0) {
            cd.tree_hash = line.substr(5);
        } else if (line.rfind("parent ", 0) == 0) {
            cd.parents.push_back(line.substr(7));
        } else if (line.rfind("author ", 0) == 0) {
            // "author Name <email> epoch tz"
            std::string rest = line.substr(7);
            // Find the last two space-separated tokens for timestamp
            auto last_space = rest.rfind(' ');
            if (last_space != std::string::npos) {
                auto second_last = rest.rfind(' ', last_space - 1);
                if (second_last != std::string::npos) {
                    cd.author    = rest.substr(0, second_last);
                    cd.timestamp = rest.substr(second_last + 1);
                } else {
                    cd.author = rest;
                }
            } else {
                cd.author = rest;
            }
        }
        // skip committer line (same as author in our stub)
    }

    cd.message = msg.str();
    // Remove trailing newline if present
    if (!cd.message.empty() && cd.message.back() == '\n') {
        cd.message.pop_back();
    }
    return cd;
}

// ─── Parse tree ─────────────────────────────────────────────────────────────

std::vector<TreeEntry> parse_tree(const std::string& raw) {
    std::vector<TreeEntry> entries;
    size_t pos = 0;

    while (pos < raw.size()) {
        // Format: "<mode> <name>\0<32-byte raw hash>"
        auto space = raw.find(' ', pos);
        if (space == std::string::npos) break;

        std::string mode_str = raw.substr(pos, space - pos);

        auto nul = raw.find('\0', space + 1);
        if (nul == std::string::npos) break;

        std::string name = raw.substr(space + 1, nul - space - 1);

        // Read 32 raw bytes of hash and convert to 64 hex chars
        size_t hash_start = nul + 1;
        if (hash_start + 32 > raw.size()) break;

        std::ostringstream hex_hash;
        hex_hash << std::hex << std::setfill('0');
        for (size_t i = 0; i < 32; ++i) {
            hex_hash << std::setw(2)
                     << static_cast<unsigned>(
                            static_cast<unsigned char>(raw[hash_start + i]));
        }

        TreeEntry entry;
        entry.mode = static_cast<uint32_t>(std::stoul(mode_str));
        entry.name = name;
        entry.hash = hex_hash.str();
        entries.push_back(std::move(entry));

        pos = hash_start + 32;
    }

    return entries;
}

// ─── Write tree ─────────────────────────────────────────────────────────────

std::string write_tree(const fs::path& repo,
                       const std::vector<TreeEntry>& entries) {
    // Sort entries by name
    auto sorted = entries;
    std::sort(sorted.begin(), sorted.end(),
              [](const TreeEntry& a, const TreeEntry& b) {
                  return a.name < b.name;
              });

    // Build tree data (without header — write_object adds it)
    std::string data;
    for (auto& e : sorted) {
        data += std::to_string(e.mode);
        data += ' ';
        data += e.name;
        data += '\0';
        data += hex_to_raw(e.hash);
    }

    return write_object(repo, ObjectType::Tree, data);
}

// ─── Write commit ───────────────────────────────────────────────────────────

std::string write_commit(const fs::path& repo, const std::string& tree_hash,
                         const std::vector<std::string>& parents,
                         const std::string& author,
                         const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
                     now.time_since_epoch())
                     .count();

    std::ostringstream oss;
    oss << "tree " << tree_hash << "\n";
    for (auto& p : parents) {
        oss << "parent " << p << "\n";
    }
    oss << "author " << author << " " << epoch << " +0000\n";
    oss << "\n";
    oss << message;

    return write_object(repo, ObjectType::Commit, oss.str());
}

// ─── Resolve hash prefix ───────────────────────────────────────────────────

std::vector<std::string> resolve_hash_prefix(const fs::path& repo,
                                             const std::string& prefix) {
    std::vector<std::string> matches;
    if (prefix.size() < 4) return matches;

    fs::path objects_dir = bts_dir(repo) / "objects";
    std::string dir_prefix = prefix.substr(0, 2);
    std::string file_prefix = prefix.substr(2);

    fs::path bucket = objects_dir / dir_prefix;
    if (!fs::is_directory(bucket)) return matches;

    for (auto& entry : fs::directory_iterator(bucket)) {
        std::string fname = entry.path().filename().string();
        if (fname.size() == 62 && fname.rfind(file_prefix, 0) == 0) {
            matches.push_back(dir_prefix + fname);
        }
    }

    std::sort(matches.begin(), matches.end());
    return matches;
}

}  // namespace bts
