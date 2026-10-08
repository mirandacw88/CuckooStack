// IStorage backed by one "key=value" text file. Every platform hands it a writable directory
// (Android internalDataPath, iOS Application Support, desktop working directory).
#pragma once

#include "Services.h"

#include <map>
#include <string>

namespace cs {

class FileStorage final : public IStorage {
public:
    explicit FileStorage(std::string path);
    std::optional<std::string> get(const std::string& key) override;
    void set(const std::string& key, const std::string& value) override;

private:
    void flush() const;
    std::string path_;
    std::map<std::string, std::string> values_;
};

} // namespace cs
