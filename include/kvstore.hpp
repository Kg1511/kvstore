#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include "net_io.hpp"  // reusing ReadExact/WriteExact/ReadString/WriteString
#include <fstream>
class KVStore {
public:
    std::string Get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) return std::string();

        if (IsExpired(it->second)) {
            data_.erase(it);
            return std::string();
        }

        return it->second.value;
    }

    void Set(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_[key] = Entry{value, std::nullopt};
    }

    bool Del(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_.erase(key) > 0;
    }

    // Sets an expiry of `seconds` from now on an existing key.
    // Returns false if the key doesn't exist (or already expired).
    bool Expire(const std::string& key, int seconds) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end() || IsExpired(it->second)) return false;

        it->second.expiry_time = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        return true;
    }
    void SaveToFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ofstream out(path, std::ios::binary);
        if (!out) return;

        uint32_t count = static_cast<uint32_t>(data_.size());
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));

        for (const auto& [key, entry] : data_) {
            if (IsExpired(entry)) continue;  // don't persist already-expired keys
            WriteStringToStream(out, key);
            WriteStringToStream(out, entry.value);
        }
    }

    void LoadFromFile(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return;  // file doesn't exist yet — fine, start empty

        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t count = 0;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));
        if (!in) return;

        for (uint32_t i = 0; i < count; ++i) {
            std::string key, value;
            if (!ReadStringFromStream(in, &key)) break;
            if (!ReadStringFromStream(in, &value)) break;
            data_[key] = Entry{value, std::nullopt};  // loaded keys never expire
        }
    }

private:
    struct Entry {
        std::string value;
        std::optional<std::chrono::steady_clock::time_point> expiry_time;
    };
    
    static void WriteStringToStream(std::ofstream& out, const std::string& s) {
        uint32_t len = static_cast<uint32_t>(s.size());
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(s.data(), static_cast<std::streamsize>(len));
    }

    static bool ReadStringFromStream(std::ifstream& in, std::string* out) {
        uint32_t len = 0;
        in.read(reinterpret_cast<char*>(&len), sizeof(len));
        if (!in) return false;
        out->resize(len);
        if (len > 0) in.read(out->data(), static_cast<std::streamsize>(len));
        return static_cast<bool>(in);
    }
    static bool IsExpired(const Entry& entry) {
        return entry.expiry_time.has_value() &&
               std::chrono::steady_clock::now() >= entry.expiry_time.value();
    }

    std::unordered_map<std::string, Entry> data_;
    std::mutex mutex_;
};