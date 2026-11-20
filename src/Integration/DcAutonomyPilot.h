#ifndef DC_AUTONOMY_PILOT_H
#define DC_AUTONOMY_PILOT_H

#include <array>
#include <cstdint>

namespace DcAutonomy
{
inline namespace V2
{
inline constexpr std::uint32_t PilotVersion = 1;
inline constexpr std::uint32_t RagefirePilot = 1;
struct RequiredEncounter
{
    std::uint32_t definition, creature;
};
struct PilotProfile
{
    std::uint32_t version, id, map, difficulty, expansion, minLevel, minItemLevel;
    std::array<RequiredEncounter, 4> encounters;
};
// Admission floors, not a claim that the party can win. Equipment is earned,
// currently equipped and unbroken. No arbitrary map/difficulty configuration.
inline constexpr PilotProfile Ragefire{
    PilotVersion, RagefirePilot, 389, 0, 0, 15, 13,
    {{{430, 11517}, {431, 11520}, {432, 11518}, {433, 11519}}}};
inline PilotProfile const* FindPilot(std::uint32_t version, std::uint32_t id)
{
    return version == PilotVersion && id == RagefirePilot ? &Ragefire : nullptr;
}
inline bool PilotAllowed(std::uint32_t version, std::uint32_t id, std::uint32_t map,
                         std::uint32_t difficulty, std::uint32_t realmExpansion)
{
    auto const* p = FindPilot(version, id);
    // This release is capped at TBC even if an operator supplies a larger cap.
    return p && realmExpansion <= 1 && p->expansion <= realmExpansion &&
           map == p->map && difficulty == p->difficulty;
}
inline std::uint32_t PilotMaxLevel(std::uint32_t realmExpansion)
{
    return realmExpansion == 0 ? 60 : realmExpansion == 1 ? 70 : 0;
}
inline bool PilotEligible(PilotProfile const& p, std::uint32_t realmExpansion,
                          std::uint32_t level, std::uint32_t equippedItemLevel)
{
    return PilotAllowed(p.version, p.id, p.map, p.difficulty, realmExpansion) &&
           level >= p.minLevel && level <= PilotMaxLevel(realmExpansion) && equippedItemLevel >= p.minItemLevel;
}
// Shared by pre-group admission and provider launch validation. A gear score
// alone can hide a missing main hand or a broken item. Other empty slots are
// allowed by this pilot; no equipment is granted or repaired here.
template <class Character>
bool PilotEquipmentReady(Character* player, std::uint8_t bag, std::uint8_t mainHand,
                         std::uint8_t first, std::uint8_t end)
{
    if (!player)
        return false;
    auto* weapon = player->GetItemByPos(bag, mainHand);
    if (!weapon || weapon->IsBroken())
        return false;
    for (auto slot = first; slot < end; ++slot)
        if (auto* item = player->GetItemByPos(bag, slot))
            if (item->IsBroken())
                return false;
    return true;
}
struct EncounterEvidence
{
    std::uint32_t definition = 0, creature = 0, index = 32;
    bool killCredit = false;
};
// Resolve indices from the actual loaded DBC/instance_encounters join, not an
// assumed Classic bit layout. Zero means missing, duplicate or incompatible
// evidence. The adapter separately verifies normal DC spawn anchors match it.
inline std::uint32_t PilotEncounterMask(PilotProfile const& p, std::array<EncounterEvidence, 4> const& rows)
{
    std::uint32_t mask = 0;
    for (auto const& required : p.encounters)
    {
        unsigned matches = 0;
        for (auto const& row : rows)
            if (row.definition == required.definition && row.creature == required.creature && row.killCredit &&
                row.index < 32)
            {
                if (mask & (1u << row.index))
                    return 0;
                mask |= 1u << row.index;
                ++matches;
            }
        if (matches != 1)
            return 0;
    }
    return mask;
}
inline bool PilotCompleted(bool allClearedSignal, std::uint32_t encounterMask, std::uint32_t requiredMask)
{
    unsigned count = 0;
    for (auto bits = requiredMask; bits; bits &= bits - 1)
        ++count;
    return allClearedSignal && count == Ragefire.encounters.size() && (encounterMask & requiredMask) == requiredMask;
}
} // namespace V2
} // namespace DcAutonomy
#endif
