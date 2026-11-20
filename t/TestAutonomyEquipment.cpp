// Shared production predicate used before group creation and at DC launch.
#include "Integration/DcAutonomyPilot.h"
#include <array>
#include <cassert>
#include <iostream>
struct Item
{
    bool present = true, broken = false;
    bool IsBroken() const { return broken; }
};
struct Character
{
    std::array<Item, 19> equipment;
    Item* GetItemByPos(std::uint8_t bag, std::uint8_t slot)
    {
        assert(bag == 255);
        return equipment.at(slot).present ? &equipment.at(slot) : nullptr;
    }
};
bool Ready(Character* c) { return DcAutonomy::PilotEquipmentReady(c, 255, 15, 0, 19); }
int main()
{
    assert(!Ready(nullptr));
    Character character;
    assert(Ready(&character));
    for (unsigned slot = 0; slot < 19; ++slot)
    {
        character = {};
        character.equipment[slot].broken = true;
        assert(!Ready(&character));
        character.equipment[slot] = {false, false};
        assert(Ready(&character) == (slot != 15));
    }
    std::cout << "PASS shared pilot equipment: missing/broken main hand and all broken equipped slots rejected; "
                 "other empty slots allowed; no item mutation\n";
}
