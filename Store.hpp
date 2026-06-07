// Store.hpp — a tiny Zustand-style global state manager for C++.
//
// Features:
//   * One global, header-only store reachable from any translation unit:
//         state::store().set<int>("hp", 100);
//         int hp = state::store().get<int>("hp");
//   * Type-safe templated get/set (a type mismatch throws).
//   * Thread-safe: many concurrent readers, exclusive writers (shared_mutex).
//   * Persisted to disk: scalars (int/long/double/bool/std::string) survive
//     restarts via a small self-contained text format — no external JSON lib.
//
// Usage:
//   #include "Store.hpp"
//   state::store().load("state.db");   // once, near program start (enables
//   autosave) state::store().set<int>("count", 1); auto c =
//   state::store().getOr<int>("count", 0);
//
// Non-persistable types (anything that isn't a scalar listed above) can still
// be stored and retrieved while the program runs; they are simply skipped on
// save.

#pragma once

#include <any>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace state {

// ── type-trait helpers ──────────────────────────────────────────────────────
// A type is "persistable" if we know how to serialize it to/from disk.
template <class T>
inline constexpr bool is_persistable_v =
    std::is_same_v<T, bool> || std::is_same_v<T, std::string> ||
    (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>);

class Store {
public:
  // --- access from anywhere -------------------------------------------------
  static Store &instance() {
    static Store s; // Meyers singleton: thread-safe init since C++11.
    return s;
  }

  // --- write ----------------------------------------------------------------
  template <class T> void set(const std::string &key, T value) {
    {
      std::unique_lock lock(mutex_);
      Entry &e = data_[key];
      // Canonicalize numbers so behaviour is identical before/after a
      // save/load round-trip (all integers stored as int64, floats as
      // double). Strings/bools/other types stored as-is.
      if constexpr (std::is_same_v<T, bool>) {
        e = Entry{std::any(value), Kind::Bool};
      } else if constexpr (std::is_same_v<T, std::string>) {
        e = Entry{std::any(value), Kind::Str};
      } else if constexpr (std::is_integral_v<T>) {
        e = Entry{std::any(static_cast<std::int64_t>(value)), Kind::Int};
      } else if constexpr (std::is_floating_point_v<T>) {
        e = Entry{std::any(static_cast<double>(value)), Kind::Float};
      } else {
        e = Entry{std::any(std::move(value)), Kind::Other};
      }
    }
    autosave();
  }

  // Convenience so set("k", "literal") stores a std::string, not const char*.
  void set(const std::string &key, const char *value) {
    set<std::string>(key, std::string(value));
  }

  // --- read -----------------------------------------------------------------
  // Throws std::out_of_range if the key is missing, std::bad_any_cast / runtime
  // error on a type mismatch.
  template <class T> T get(const std::string &key) const {
    std::shared_lock lock(mutex_);
    auto it = data_.find(key);
    if (it == data_.end())
      throw std::out_of_range("state::Store: missing key '" + key + "'");
    return coerce<T>(it->second);
  }

  // Returns std::nullopt instead of throwing when the key is missing or the
  // type doesn't match.
  template <class T> std::optional<T> tryGet(const std::string &key) const {
    std::shared_lock lock(mutex_);
    auto it = data_.find(key);
    if (it == data_.end())
      return std::nullopt;
    try {
      return coerce<T>(it->second);
    } catch (...) {
      return std::nullopt;
    }
  }

  // Returns a fallback when the key is missing or mismatched.
  template <class T> T getOr(const std::string &key, T fallback) const {
    auto v = tryGet<T>(key);
    return v ? *v : fallback;
  }

  // --- queries --------------------------------------------------------------
  bool has(const std::string &key) const {
    std::shared_lock lock(mutex_);
    return data_.find(key) != data_.end();
  }

  void erase(const std::string &key) {
    {
      std::unique_lock lock(mutex_);
      data_.erase(key);
    }
    autosave();
  }

  void clear() {
    {
      std::unique_lock lock(mutex_);
      data_.clear();
    }
    autosave();
  }

  std::size_t size() const {
    std::shared_lock lock(mutex_);
    return data_.size();
  }

  std::vector<std::string> keys() const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> out;
    out.reserve(data_.size());
    for (const auto &[k, _] : data_)
      out.push_back(k);
    return out;
  }

  // --- persistence ----------------------------------------------------------
  // Point the store at a file. If it exists, its contents are loaded. The path
  // is remembered so future writes autosave to it.
  void load(const std::string &path) {
    std::unique_lock lock(mutex_);
    path_ = path;
    autosave_ = true;
    std::ifstream in(path);
    if (!in)
      return; // first run: nothing to load yet.
    data_.clear();
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty())
        continue;
      // Format: <kind>\t<key>\t<value>   (key & value are escaped)
      auto t1 = line.find('\t');
      if (t1 == std::string::npos)
        continue;
      auto t2 = line.find('\t', t1 + 1);
      if (t2 == std::string::npos)
        continue;
      char kind = line[0];
      std::string key = unescape(line.substr(t1 + 1, t2 - t1 - 1));
      std::string val = unescape(line.substr(t2 + 1));
      data_[key] = decode(kind, val);
    }
  }

  // Explicitly write to the configured path (or a one-off path). Returns false
  // if no path is known / the file can't be opened.
  bool save() const {
    std::shared_lock lock(mutex_);
    return saveLocked(path_);
  }
  bool save(const std::string &path) const {
    std::shared_lock lock(mutex_);
    return saveLocked(path);
  }

  void setAutosave(bool on) {
    std::unique_lock lock(mutex_);
    autosave_ = on;
  }

  ~Store() {
    if (autosave_ && !path_.empty())
      saveLocked(path_);
  }

private:
  enum class Kind { Int, Float, Bool, Str, Other };
  struct Entry {
    std::any value;
    Kind kind = Kind::Other;
  };

  Store() = default;
  Store(const Store &) = delete;
  Store &operator=(const Store &) = delete;

  // Convert a stored entry into the requested type T, with sensible numeric
  // coercion so get<int> still works after values round-trip through int64.
  template <class T> static T coerce(const Entry &e) {
    if constexpr (std::is_same_v<T, bool>) {
      if (e.kind != Kind::Bool)
        throw std::runtime_error("state::Store: type mismatch (expected bool)");
      return std::any_cast<bool>(e.value);
    } else if constexpr (std::is_same_v<T, std::string>) {
      if (e.kind != Kind::Str)
        throw std::runtime_error(
            "state::Store: type mismatch (expected string)");
      return std::any_cast<std::string>(e.value);
    } else if constexpr (std::is_integral_v<T>) {
      if (e.kind == Kind::Int)
        return static_cast<T>(std::any_cast<std::int64_t>(e.value));
      if (e.kind == Kind::Float)
        return static_cast<T>(std::any_cast<double>(e.value));
      throw std::runtime_error(
          "state::Store: type mismatch (expected integral)");
    } else if constexpr (std::is_floating_point_v<T>) {
      if (e.kind == Kind::Float)
        return static_cast<T>(std::any_cast<double>(e.value));
      if (e.kind == Kind::Int)
        return static_cast<T>(std::any_cast<std::int64_t>(e.value));
      throw std::runtime_error(
          "state::Store: type mismatch (expected floating point)");
    } else {
      // Arbitrary in-memory type: exact match required.
      return std::any_cast<T>(e.value);
    }
  }

  void autosave() {
    std::shared_lock lock(mutex_);
    if (autosave_ && !path_.empty())
      saveLocked(path_);
  }

  // Caller must hold a lock. Writes every persistable entry.
  bool saveLocked(const std::string &path) const {
    if (path.empty())
      return false;
    std::ofstream out(path, std::ios::trunc);
    if (!out)
      return false;
    for (const auto &[key, e] : data_) {
      std::string val;
      char kind;
      switch (e.kind) {
      case Kind::Int:
        kind = 'i';
        val = std::to_string(std::any_cast<std::int64_t>(e.value));
        break;
      case Kind::Float: {
        kind = 'd';
        std::ostringstream ss;
        ss.precision(17);
        ss << std::any_cast<double>(e.value);
        val = ss.str();
        break;
      }
      case Kind::Bool:
        kind = 'b';
        val = std::any_cast<bool>(e.value) ? "1" : "0";
        break;
      case Kind::Str:
        kind = 's';
        val = std::any_cast<std::string>(e.value);
        break;
      case Kind::Other:
      default:
        continue; // not serializable — skip.
      }
      out << kind << '\t' << escape(key) << '\t' << escape(val) << '\n';
    }
    return static_cast<bool>(out);
  }

  static Entry decode(char kind, const std::string &val) {
    switch (kind) {
    case 'i':
      return Entry{std::any(static_cast<std::int64_t>(std::stoll(val))),
                   Kind::Int};
    case 'd':
      return Entry{std::any(std::stod(val)), Kind::Float};
    case 'b':
      return Entry{std::any(val == "1"), Kind::Bool};
    case 's':
    default:
      return Entry{std::any(val), Kind::Str};
    }
  }

  // Escape tabs/newlines/backslashes so each record stays on one line.
  static std::string escape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
      switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      default:
        out += c;
        break;
      }
    }
    return out;
  }

  static std::string unescape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '\\' && i + 1 < s.size()) {
        switch (s[++i]) {
        case 't':
          out += '\t';
          break;
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case '\\':
          out += '\\';
          break;
        default:
          out += s[i];
          break;
        }
      } else {
        out += s[i];
      }
    }
    return out;
  }

  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, Entry> data_;
  std::string path_;
  bool autosave_ = false;
};

// Free-function shortcut so call sites read like Zustand:
// state::store().get<T>(...)
inline Store &store() { return Store::instance(); }

} // namespace state
