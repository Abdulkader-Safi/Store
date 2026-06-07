// main.cpp — demo of the Zustand-style global store.
#include <iostream>
#include <thread>
#include <vector>

#include "Store.hpp"
#include "player.hpp"

int main() {
  auto &store = state::store();

  // Point the store at a file. If it exists from a previous run, it loads now
  // and every later set() autosaves to it.
  store.load("state.db");

  std::cout << "=== run start (loaded " << store.size() << " keys) ===\n";

  // First run: seed values. Later runs: these are already loaded from disk.
  if (!store.has("name"))
    store.set("name", "Hero"); // const char* -> std::string
  if (!store.has("hp"))
    store.set<int>("hp", 100);

  // Persisted counter: increments by 1 every time you run the program.
  int runs = store.getOr<int>("runs", 0) + 1;
  store.set<int>("runs", runs);
  std::cout << "this program has now run " << runs << " time(s)\n";

  // Cross-file access: player.cpp reads/writes the same store.
  player_report();
  player_take_damage(30);
  player_report();

  // Type safety: get<T> with the wrong type throws.
  store.set<double>("ratio", 0.75);
  std::cout << "ratio as double = " << store.get<double>("ratio") << '\n';
  try {
    store.get<std::string>("ratio"); // wrong type
  } catch (const std::exception &ex) {
    std::cout << "caught expected error: " << ex.what() << '\n';
  }

  // Thread safety: hammer a counter from several threads.
  store.set<int>("threaded", 0);
  {
    std::vector<std::thread> pool;
    for (int t = 0; t < 8; ++t) {
      pool.emplace_back([&store] {
        for (int i = 0; i < 1000; ++i) {
          // read-modify-write; each set/get takes the lock internally.
          store.set<int>("threaded", store.getOr<int>("threaded", 0) + 1);
        }
      });
    }
    for (auto &th : pool)
      th.join();
  }
  std::cout << "threaded counter (no crash/UB) = " << store.get<int>("threaded")
            << '\n';

  std::cout << "=== run end (saved " << store.size()
            << " keys to state.db) ===\n";
  return 0;
}
