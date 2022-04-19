#pragma once

#include <cobalt/asl/algorithm.hpp>
#include <cobalt/asl/convert.hpp>
#include <cobalt/asl/hash_map.hpp>

#include <istream>

namespace cobalt::core {

/// @brief config class that holds a map of keys and values that can be read from .ini file
class config {
public:
    /// @brief Create config from input stream
    ///
    /// @param input Input stream
    /// @return config Config object
    static config from_stream(std::istream& input) {
        return config(input);
    }

    /// @brief Get config value by key
    ///
    /// @tparam T Expected type
    /// @param key Key to read
    /// @return T Result
    template<typename T = std::string>
    T get(std::string key) const {
        return asl::from_string<T>(_setting_map.at(key));
    }

    /// @brief Set the default value into settings map, does not override if already present
    ///
    /// @tparam T Value type
    /// @param key Key to insert
    /// @param value Value
    template<typename T>
    void set_default(std::string key, T value) {
        _setting_map.emplace(key, std::to_string(value));
    }

private:
    config(std::istream& input) {
        std::string temp;
        while (std::getline(input, temp)) {
            if (temp.empty()) {
                continue;
            }

            auto pos = temp.find('=');
            if (pos == std::string::npos) {
                continue;
            }

            auto key = asl::trim(temp.substr(0, pos));
            auto value = asl::trim(temp.substr(pos + 1));
            _setting_map.emplace(key, value);
        }
    }

    asl::hash_map<std::string, std::string> _setting_map;
};

} // namespace cobalt::core