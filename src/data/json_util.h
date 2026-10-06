#pragma once

// Shared JSON reading and file helpers for the data library. Each store
// throws its own exception type, so the helpers are templates on it.
// Internal to combat_data: nlohmann/json is a private dependency.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace combat::json_util {

// ordered_json keeps keys in the order written, so files read top to bottom.
using json = nlohmann::ordered_json;

template <class Error>
const json& requireField(const json& object, const char* key, const std::string& context)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        throw Error(context + ": missing field \"" + key + "\".");
    }
    return *it;
}

template <class Error>
void requireObject(const json& value, const std::string& context)
{
    if (!value.is_object()) {
        throw Error(context + ": must be an object.");
    }
}

template <class Error>
const json& requireArray(const json& object, const char* key, const std::string& context)
{
    const json& value = requireField<Error>(object, key, context);
    if (!value.is_array()) {
        throw Error(context + ": field \"" + key + "\" must be an array.");
    }
    return value;
}

template <class Error>
int integerValue(const json& value, const char* key, const std::string& context)
{
    if (!value.is_number_integer()) {
        throw Error(context + ": field \"" + key + "\" must be an integer.");
    }
    if (value.is_number_unsigned()) {
        const auto raw = value.get<std::uint64_t>();
        if (raw > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            throw Error(context + ": field \"" + key + "\" is out of range.");
        }
        return static_cast<int>(raw);
    }
    const auto raw = value.get<std::int64_t>();
    if (raw < std::numeric_limits<int>::min() || raw > std::numeric_limits<int>::max()) {
        throw Error(context + ": field \"" + key + "\" is out of range.");
    }
    return static_cast<int>(raw);
}

template <class Error>
int readInt(const json& object, const char* key, const std::string& context)
{
    return integerValue<Error>(requireField<Error>(object, key, context), key, context);
}

// Missing or null is empty.
template <class Error>
std::optional<int> readOptionalInt(const json& object, const char* key, const std::string& context)
{
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return std::nullopt;
    }
    return integerValue<Error>(*it, key, context);
}

template <class Error>
int readIntOr(const json& object, const char* key, int fallback, const std::string& context)
{
    return readOptionalInt<Error>(object, key, context).value_or(fallback);
}

template <class Error>
std::string readString(const json& object, const char* key, const std::string& context)
{
    const json& value = requireField<Error>(object, key, context);
    if (!value.is_string()) {
        throw Error(context + ": field \"" + key + "\" must be a string.");
    }
    return value.get<std::string>();
}

template <class Error>
std::string readOptionalString(const json& object, const char* key, const std::string& context)
{
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return {};
    }
    if (!it->is_string()) {
        throw Error(context + ": field \"" + key + "\" must be a string.");
    }
    return it->get<std::string>();
}

template <class Error>
bool readBool(const json& object, const char* key, const std::string& context)
{
    const json& value = requireField<Error>(object, key, context);
    if (!value.is_boolean()) {
        throw Error(context + ": field \"" + key + "\" must be a boolean.");
    }
    return value.get<bool>();
}

template <class Error>
bool readBoolOr(const json& object, const char* key, bool fallback, const std::string& context)
{
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return fallback;
    }
    if (!it->is_boolean()) {
        throw Error(context + ": field \"" + key + "\" must be a boolean.");
    }
    return it->get<bool>();
}

// Missing is empty. Every entry must be a string.
template <class Error>
std::vector<std::string> readStringList(const json& object, const char* key, const std::string& context)
{
    std::vector<std::string> list;
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return list;
    }
    if (!it->is_array()) {
        throw Error(context + ": field \"" + key + "\" must be an array.");
    }
    for (std::size_t i = 0; i < it->size(); ++i) {
        if (!(*it)[i].is_string()) {
            throw Error(context + " " + key + " " + std::to_string(i + 1) + ": must be a string.");
        }
        list.push_back((*it)[i].get<std::string>());
    }
    return list;
}

template <class Error>
json parseDocument(const std::string& text, const std::string& what)
{
    json document;
    try {
        document = json::parse(text);
    } catch (const json::parse_error& error) {
        throw Error("The " + what + " file is not valid JSON: " + error.what());
    }
    if (!document.is_object()) {
        throw Error("The " + what + " file must contain a JSON object.");
    }
    return document;
}

// Throws when the file exists but cannot be read. Empty when it is missing
// and missingIsEmpty is set; otherwise a missing file throws too.
template <class Error>
std::optional<std::string> readTextFile(const std::filesystem::path& path, bool missingIsEmpty)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (ec) {
            throw Error("Could not check " + path.string() + ": " + ec.message());
        }
        if (missingIsEmpty) {
            return std::nullopt;
        }
        throw Error("Could not open " + path.string() + " for reading.");
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw Error("Could not open " + path.string() + " for reading.");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        throw Error("Could not read " + path.string() + ".");
    }
    return buffer.str();
}

// Writes a temporary file in the same folder and renames it over the real
// one, so an interrupted save keeps the previous file.
template <class Error>
void writeFileAtomically(const std::filesystem::path& path, const std::string& text)
{
    std::error_code ec;
    const auto folder = path.parent_path();
    if (!folder.empty()) {
        std::filesystem::create_directories(folder, ec);
        if (ec) {
            throw Error("Could not create " + folder.string() + ": " + ec.message());
        }
    }

    auto tempPath = path;
    tempPath += ".tmp";
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw Error("Could not open " + tempPath.string() + " for writing.");
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(tempPath, ec);
            throw Error("Could not write " + tempPath.string() + ".");
        }
    }

    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        const std::string message = ec.message();
        std::filesystem::remove(tempPath, ec);
        throw Error("Could not replace " + path.string() + ": " + message);
    }
}

}  // namespace combat::json_util
