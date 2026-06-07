// player.cpp — a different translation unit talking to the global store.
#include "player.hpp"

#include <iostream>

#include "Store.hpp"

void player_take_damage(int amount) {
  // Read the current value (default 0 if unset), subtract, write it back.
  int hp = state::store().getOr<int>("hp", 0);
  state::store().set<int>("hp", hp - amount);
  std::cout << "[player] took " << amount << " damage -> hp now "
            << state::store().get<int>("hp") << '\n';
}

void player_report() {
  auto name = state::store().getOr<std::string>("name", "<unknown>");
  auto hp = state::store().getOr<int>("hp", 0);
  std::cout << "[player] " << name << " has " << hp << " hp\n";
}
