// player.hpp — a separate module that reads/writes the SAME global store.
// Demonstrates that state set in main.cpp is visible here and vice-versa.
#pragma once

void player_take_damage(int amount);  // mutates "hp" in the global store
void player_report();                 // reads "hp"/"name" back out
