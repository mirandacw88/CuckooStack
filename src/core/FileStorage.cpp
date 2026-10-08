#include "FileStorage.h"

#include <cstdio>
#include <fstream>

namespace cs {

FileStorage::FileStorage(std::string path) : path_(std::move(path)) {
    std::ifstream in(path_);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq != std::string::npos) values_[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

std::optional<std::string> FileStorage::get(const std::string& key) {
    const auto it = values_.find(key);
    if (it == values_.end()) return std::nullopt;
    return it->second;
}

void FileStorage::set(const std::string& key, const std::string& value) {
    values_[key] = value;
    flush();
}

void FileStorage::flush() const {
    // write-then-rename so a crash mid-write never loses the record
    const std::string tmp = path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return;
        for (const auto& [k, v] : values_) out << k << '=' << v << '\n';
    }
    std::rename(tmp.c_str(), path_.c_str());
}

} // namespace cs
