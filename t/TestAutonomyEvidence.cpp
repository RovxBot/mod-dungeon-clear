// Executes the production RequiredMask adapter body extracted by check_autonomy.
// Engine data are fakes; no real DBC, map navigation, kills or game server.
#include "Integration/DcAutonomyPilot.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace DcAutonomy;
enum Difficulty { DUNGEON_DIFFICULTY_NORMAL };
inline constexpr unsigned ENCOUNTER_CREDIT_KILL_CREATURE = 0;
enum class DungeonAnchorKind { Boss, Objective };
struct Definition { unsigned id, mapId, difficulty, encounterIndex; };
struct Encounter { Definition const* dbcEntry; unsigned creditEntry, creditType; };
struct Anchor
{
    unsigned entry, mapId, encounterIndex;
    DungeonAnchorKind kind = DungeonAnchorKind::Boss;
    bool skipByDesign = false;
};
struct ObjectManager
{
    std::vector<Encounter const*> Rows;
    std::vector<Encounter const*> const* GetDungeonEncounterList(unsigned map, Difficulty)
    {
        return map == 389 ? &Rows : nullptr;
    }
} manager;
#define sObjectMgr (&manager)
struct BossSpawnIndex
{
    static inline std::vector<Anchor> Anchors;
    static auto const& Get(unsigned, Difficulty) { return Anchors; }
};
#include "AutonomyEvidenceBody.h"
int main()
{
    // Deliberately non-contiguous bit indices: no assumed 0..2/0..3 layout.
    std::array<Definition, 4> definitions{{{430,389,0,0}, {431,389,0,2}, {432,389,0,4}, {433,389,0,31}}};
    std::array<Encounter, 4> encounters{};
    auto setup = [&]
    {
        manager.Rows.clear(); BossSpawnIndex::Anchors.clear();
        for (unsigned n = 0; n < 4; ++n)
        {
            encounters[n] = {&definitions[n], Ragefire.encounters[n].creature, 0};
            manager.Rows.push_back(&encounters[n]);
            BossSpawnIndex::Anchors.push_back({encounters[n].creditEntry, 389, definitions[n].encounterIndex});
        }
    };
    setup();
    assert(RequiredMask(Ragefire) == 0x80000015u);
    for (unsigned fault = 0; fault < 14; ++fault)
    {
        setup();
        auto saved = definitions[3];
        switch (fault)
        {
        case 0: manager.Rows.pop_back(); break;
        case 1: manager.Rows.push_back(manager.Rows.back()); break;
        case 2: manager.Rows[3] = nullptr; break;
        case 3: encounters[3].dbcEntry = nullptr; break;
        case 4: definitions[3].mapId = 574; break;
        case 5: definitions[3].difficulty = 1; break;
        case 6: definitions[3].id = 202; break;
        case 7: encounters[3].creditEntry = 23954; break;
        case 8: definitions[3].encounterIndex = 2; break;
        case 9: definitions[3].encounterIndex = 32; break;
        case 10: encounters[3].creditType = 1; break;
        case 11: BossSpawnIndex::Anchors.pop_back(); break;
        case 12: BossSpawnIndex::Anchors[3].kind = DungeonAnchorKind::Objective; break;
        case 13: BossSpawnIndex::Anchors[3].encounterIndex = 3; break;
        }
        assert(RequiredMask(Ragefire) == 0);
        definitions[3] = saved;
    }
    std::cout << "PASS actual encounter/spawn adapter: four RFC definitions, dynamic bit indices, incompatible "
                 "data fail closed; injected data only\n";
}
