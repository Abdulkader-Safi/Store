# Store

A tiny Zustand-style global state manager for C++, in a single header.

Set a value under a string key from anywhere in your program, read it back type-safely from any other file, and have scalars survive a restart. No external dependencies, just `Store.hpp` and C++23.

```cpp
#include "Store.hpp"

state::store().set<int>("hp", 100);
int hp = state::store().get<int>("hp");
```

## What it does

- **One global store.** `state::store()` returns the same instance everywhere, so `main.cpp` and `player.cpp` read and write the same keys. It's a Meyers singleton, so initialisation is thread-safe.
- **Type-safe reads.** `get<T>` checks the stored type. Ask for the wrong type and it throws instead of handing you garbage.
- **Thread-safe.** A `shared_mutex` lets many readers run at once and gives writers exclusive access. The demo hammers a counter from 8 threads with no crash.
- **Persisted to disk.** Call `load("state.db")` once and every later write autosaves. Scalars (`int`, `long`, `double`, `bool`, `std::string`) round-trip through a small text format with no JSON library.
- **In-memory for anything else.** Non-scalar types still work while the program runs. They're just skipped when saving.

## Quick start

```cpp
#include "Store.hpp"

int main() {
  auto &store = state::store();

  store.load("state.db");          // load if the file exists, autosave from here on

  store.set("name", "Hero");       // const char* is stored as std::string
  store.set<int>("hp", 100);

  int runs = store.getOr<int>("runs", 0) + 1;
  store.set<int>("runs", runs);    // a counter that climbs every run
}
```

Run the program twice and `runs` goes from 1 to 2: the value came back off disk.

## API

| Call                      | What it does                                                                        |
| ------------------------- | ----------------------------------------------------------------------------------- |
| `set<T>(key, value)`      | Store `value` under `key`. Integers are normalised to int64, floats to double.      |
| `get<T>(key)`             | Read `key` as `T`. Throws `std::out_of_range` if missing, or a type-mismatch error. |
| `tryGet<T>(key)`          | Same read, but returns `std::optional<T>` (empty on missing or wrong type).         |
| `getOr<T>(key, fallback)` | Read `key`, or return `fallback` if it's missing or mismatched.                     |
| `has(key)`                | Is the key present?                                                                 |
| `erase(key)` / `clear()`  | Remove one key, or all of them.                                                     |
| `size()` / `keys()`       | Count, or a list of every key.                                                      |
| `load(path)`              | Read from `path` if it exists, remember it, and turn on autosave.                   |
| `save()` / `save(path)`   | Write now, to the configured path or a one-off path.                                |
| `setAutosave(on)`         | Turn autosave on or off.                                                            |

`state::store()` is a free-function shortcut for `Store::instance()`, so call sites read like Zustand.

## Persistence format

Each record is one line: `<kind>\t<key>\t<value>`, where kind is `i`, `d`, `b`, or `s`. Keys and values are escaped so tabs and newlines stay on a single line. The store also saves once in its destructor if autosave is on, so a clean exit never loses state.

Numbers are canonicalised on the way in, so `get<int>` behaves the same before and after a save/load round-trip.

## Build

Needs CMake 3.20+ and a C++23 compiler.

```bash
cmake -B build
cmake --build build
./build/main
```

The demo seeds a player, applies damage across files, shows a type mismatch being caught, and stress-tests the lock from 8 threads.

## Files

- `Store.hpp` — the whole store, header-only.
- `main.cpp` — demo and walkthrough of every feature.
- `player.hpp` / `player.cpp` — a second translation unit using the same store.
- `CMakeLists.txt` — C++23, warnings on, links the threading library.

## Notes

- `set`/`get` lock internally, but a read-modify-write (`set(k, get(k) + 1)`) is two separate locked operations, not one atomic step. The threaded demo still lands on a valid value because each call is individually safe, but if you need a true atomic increment across threads, guard the read and write together yourself.
- Only scalars persist. Store a custom struct and it lives in memory for the session, then drops on save.
