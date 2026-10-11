#pragma once

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Minimal command-line parsing: positional arguments plus `--flag` and `--key value`.
class Cli {
 public:
    Cli(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg.substr(0, 2) == "--") {
                const auto key = std::string(arg.substr(2));
                if (i + 1 < argc && std::string_view(argv[i + 1]).substr(0, 2) != "--") {
                    options_.emplace_back(key, argv[++i]);
                } else {
                    options_.emplace_back(key, "");
                }
            } else {
                positional_.emplace_back(arg);
            }
        }
    }

    const std::vector<std::string>& positional() const { return positional_; }

    bool has(std::string_view key) const { return find(key) != nullptr; }

    std::string get(std::string_view key, std::string fallback) const {
        const std::string* value = find(key);
        return value != nullptr && !value->empty() ? *value : fallback;
    }

    double get_double(std::string_view key, double fallback) const {
        const std::string* value = find(key);
        return value != nullptr && !value->empty() ? std::atof(value->c_str()) : fallback;
    }

    long get_long(std::string_view key, long fallback) const {
        const std::string* value = find(key);
        return value != nullptr && !value->empty() ? std::atol(value->c_str()) : fallback;
    }

 private:
    const std::string* find(std::string_view key) const {
        for (const auto& [k, v] : options_) {
            if (k == key) {
                return &v;
            }
        }
        return nullptr;
    }

    std::vector<std::string> positional_;
    std::vector<std::pair<std::string, std::string>> options_;
};
