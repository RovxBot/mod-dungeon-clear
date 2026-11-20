#include "DcAutonomyRuntime.h"
#include "DcAutonomyController.h"

#include "Ai/Dungeon/DungeonClear/Action/DcActionShared.h"
#include "Ai/Dungeon/DungeonClear/Action/DungeonClearChatActions.h"
#include "Ai/Dungeon/DungeonClear/Data/BossSpawnIndex.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPlayerbotCompat.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRunProgress.h"
#include "DcModuleEnable.h"
#include "DBCStructure.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Item.h"
#include "LFGMgr.h"
#include "Map.h"
#include "MapMgr.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "WorldScript.h"
#include <ctime>
#include <thread>

namespace DcAutonomy
{
namespace
{
std::atomic<bool> ActiveAutonomy{false};
}
bool Owns(Player* player)
{
    if (!ActiveAutonomy.load(std::memory_order_acquire))
        return false;
    auto* owner = player ? DcLeaderSignal::FindRunOwner(player) : nullptr;
    auto* ai = owner ? GET_PLAYERBOT_AI(owner) : nullptr;
    return ai && DcRun::Of(ai).autonomyRun;
}
namespace
{
Player* Find(std::uint32_t guid)
{
    return ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(guid));
}
bool Bot(Player* p)
{
    auto* ai = p ? GET_PLAYERBOT_AI(p) : nullptr;
    return p && ai && !p->IsGameMaster() && !DcPlayerbotCompat::IsHumanControlled(p) &&
           !DcPlayerbotCompat::IsHumanControlled(ai->GetMaster());
}
std::uint32_t RequiredMask(PilotProfile const& profile)
{
    auto const* definitions = sObjectMgr->GetDungeonEncounterList(profile.map, Difficulty(profile.difficulty));
    if (!definitions || definitions->size() != profile.encounters.size())
        return 0;
    std::array<EncounterEvidence, 4> evidence{};
    std::size_t at = 0;
    for (auto const* row : *definitions)
    {
        if (!row || !row->dbcEntry || row->dbcEntry->mapId != profile.map ||
            row->dbcEntry->difficulty != profile.difficulty)
            return 0;
        evidence[at++] = {row->dbcEntry->id, row->creditEntry, row->dbcEntry->encounterIndex,
                          row->creditType == ENCOUNTER_CREDIT_KILL_CREATURE};
    }
    auto mask = PilotEncounterMask(profile, evidence);
    auto const& anchors = BossSpawnIndex::Get(profile.map, Difficulty(profile.difficulty));
    if (!mask || anchors.size() != evidence.size())
        return 0;
    for (auto const& row : evidence)
    {
        unsigned matches = 0;
        for (auto const& anchor : anchors)
            matches += anchor.mapId == profile.map && anchor.entry == row.creature &&
                       anchor.encounterIndex == row.index && anchor.kind == DungeonAnchorKind::Boss &&
                       !anchor.skipByDesign;
        if (matches != 1)
            return 0;
    }
    return mask;
}
class EnginePort final : public Port
{
public:
    std::thread::id thread;
    DcRunProgress::Mark progress;
    std::uint64_t lastProgress = 0;
    std::array<bool, 5> addedAoe{};
    bool OnWorldThread() const override { return std::this_thread::get_id() == thread; }
    bool Enabled() const override
    {
        return DcModule::IsEnabled() && DcSettings::GetBool(ObjectGuid::Empty, "Autonomy.Enable");
    }
    std::uint64_t Now() const override { return static_cast<std::uint64_t>(std::time(nullptr)); }
    std::string Validate(Permit const& p) const override
    {
        auto const* profile = FindPilot(p.profileVersion, p.profile);
        if (!profile || !PilotAllowed(p.profileVersion, p.profile, p.map, p.difficulty, p.realmExpansion))
            return "unsupported_pilot_profile";
        if (p.realmExpansion != DcSettings::GetUInt(ObjectGuid::Empty, "Autonomy.MaxExpansion"))
            return "realm_progression_cap_mismatch";
        // Context registration happens on DC's first world tick. An early or
        // mismatched provider call must not dereference a missing run value.
        for (auto guid : p.members)
        {
            auto* member = Find(guid);
            auto* ai = member ? GET_PLAYERBOT_AI(member) : nullptr;
            if (!ai || !ai->GetAiObjectContext() ||
                !ai->GetAiObjectContext()->GetValue<DcRunState&>(DcKey::RunState))
                return "dungeon_clear_context_unavailable";
        }
        auto* leader = Find(p.leader);
        if (!Bot(leader))
            return "leader_missing_or_human";
        auto* group = leader->GetGroup();
        if (!group || group->GetGUID().GetCounter() != p.group || group->GetMembersCount() != 5 ||
            group->GetLeaderGUID() != leader->GetGUID() || group->isRaidGroup() || group->isLFGGroup() ||
            group->isBGGroup() || group->GetDungeonDifficulty() != DUNGEON_DIFFICULTY_NORMAL)
            return "authoritative_group_mismatch";
        if (!PlayerbotAI::IsTank(leader) || DcLeaderSignal::FindLeaderTank(leader) != leader)
            return "selected_tank_not_dc_leader";
        if (!leader->GetMap() || !leader->GetMap()->IsDungeon() || leader->GetMap()->IsRaid() ||
            leader->GetMapId() != profile->map || leader->GetInstanceId() != p.instance ||
            leader->GetMap()->GetDifficulty() != DUNGEON_DIFFICULTY_NORMAL)
            return "unsupported_map_instance_or_difficulty";
        auto* instance = leader->GetInstanceScript();
        if (!instance)
            return "instance_script_unavailable";
        auto required = RequiredMask(*profile);
        if (!required)
            return "pilot_encounter_definitions_or_spawns_incompatible";
        if (DcRun::Of(GET_PLAYERBOT_AI(leader)).autonomyRun != p.nonce &&
            (instance->GetCompletedEncounterMask() & required))
            return "pilot_instance_not_fresh";
        unsigned tanks = 0, heals = 0, dps = 0;
        for (auto guid : p.members)
        {
            auto* m = Find(guid);
            if (!Bot(m) || m->GetGroup() != group || !group->IsMember(m->GetGUID()) || m->GetOriginalGroup() ||
                !m->IsInWorld() || m->IsBeingTeleported() || !m->IsAlive() || m->GetMap() != leader->GetMap() ||
                m->GetDungeonDifficulty() != DUNGEON_DIFFICULTY_NORMAL || m->InBattlegroundQueue() ||
                sLFGMgr->GetState(m->GetGUID()) != lfg::LFG_STATE_NONE)
                return "participant_changed_or_unavailable";
            tanks += PlayerbotAI::IsTank(m);
            heals += PlayerbotAI::IsHeal(m);
            dps += PlayerbotAI::IsDps(m);
            if (m->GetTeamId() != leader->GetTeamId())
                return "faction_mismatch";
            if (!PilotEligible(*profile, p.realmExpansion, m->GetLevel(), GET_PLAYERBOT_AI(m)->GetEquipGearScore(m)))
                return "pilot_level_or_equipment_ineligible";
            if (!PilotEquipmentReady(m, INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND,
                                     EQUIPMENT_SLOT_START, EQUIPMENT_SLOT_END))
                return "pilot_equipment_missing_or_broken";
            // loginCheck avoids the core's intentional ALREADY_IN_MAP refusal.
            if (sMapMgr->PlayerCannotEnter(profile->map, m, true) != Map::CAN_ENTER)
                return "ordinary_instance_access_denied";
            auto const& run = DcRun::Of(GET_PLAYERBOT_AI(m));
            if (run.enabled && run.autonomyRun != p.nonce)
                return "conflicting_dungeon_clear_run";
        }
        return tanks == 1 && heals == 1 && dps == 3 ? "" : "role_roster_mismatch";
    }
    bool Initialize(Permit const& p) override
    {
        auto* leader = Find(p.leader);
        auto* ai = GET_PLAYERBOT_AI(leader);
        // No pull-mode daze immunity or dynamic pull activation in this pilot.
        ai->GetAiObjectContext()->GetValue<uint32>(DcKey::PullSetting)->Set(0u);
        addedAoe.fill(false);
        if (!DcOnAction(ai).Initialize(""))
            return false;
        DcRun::Of(ai).autonomyRun = p.nonce;
        ActiveAutonomy.store(true, std::memory_order_release);
        for (std::size_t i = 0; i < p.members.size(); ++i)
        {
            auto* memberAI = GET_PLAYERBOT_AI(Find(p.members[i]));
            addedAoe[i] = !memberAI->HasStrategy("avoid aoe", BOT_STATE_COMBAT);
            if (addedAoe[i])
                memberAI->ChangeStrategy("+avoid aoe", BOT_STATE_COMBAT);
            if (!memberAI->HasStrategy("dungeon clear", BOT_STATE_NON_COMBAT))
                memberAI->ChangeStrategy("+dungeon clear", BOT_STATE_NON_COMBAT);
            if (!memberAI->HasStrategy("dungeon clear combat", BOT_STATE_COMBAT))
                memberAI->ChangeStrategy("+dungeon clear combat", BOT_STATE_COMBAT);
        }
        progress = {};
        lastProgress = Now();
        return true;
    }
    Status Observe(Permit const& p) override
    {
        auto s = Controller::Identity(p);
        auto* leader = Find(p.leader);
        auto* ai = leader ? GET_PLAYERBOT_AI(leader) : nullptr;
        if (!ai || DcRun::Of(ai).autonomyRun != p.nonce)
        {
            s.state = State::Failed;
            s.reason = "run_owner_lost_manual_reconciliation";
            return s;
        }
        auto const& run = DcRun::Of(ai);
        s.map = leader->GetMapId();
        s.instance = leader->GetInstanceId();
        if (s.map != p.map || s.instance != p.instance)
        {
            s.state = State::Failed;
            s.reason = "run_instance_lost";
            return s;
        }
        auto* instance = leader->GetInstanceScript();
        s.encounters = instance ? instance->GetCompletedEncounterMask() : 0;
        auto const* profile = FindPilot(p.profileVersion, p.profile);
        s.requiredEncounters = profile ? RequiredMask(*profile) : 0;
        if (!run.enabled)
        {
            bool complete = PilotCompleted(run.autonomyEnd == DcActionShared::kReasonAllCleared,
                                           s.encounters, s.requiredEncounters);
            s.state = complete ? State::Completed : State::Failed;
            s.reason = run.autonomyEnd.empty() ? "disabled_without_success_evidence" : run.autonomyEnd;
            if (!complete && run.autonomyEnd == DcActionShared::kReasonAllCleared)
                s.reason = "terminal_missing_pilot_encounter_evidence";
        }
        else
        {
            s.state = run.paused ? State::Paused : State::Running;
            s.reason = run.paused ? run.pauseReason : "running";
            if (DcRunProgress::Detect(leader, ai, progress))
                lastProgress = Now();
        }
        s.objectives = progress.anchors;
        s.lastProgress = lastProgress;
        return s;
    }
    bool Teardown(Permit const& p) override
    {
        auto* leader = Find(p.leader);
        auto* ai = leader ? GET_PLAYERBOT_AI(leader) : nullptr;
        // Losing the owner is uncertainty, not proof that outstanding work ended.
        if (!ai)
            return false;
        auto& run = DcRun::Of(ai);
        // Successful manual initialization clears our marker. That positively
        // establishes handoff; never tear down the replacement player's run.
        if (run.autonomyRun != p.nonce)
        {
            if (!run.autonomyRun)
                ActiveAutonomy.store(false, std::memory_order_release);
            return run.autonomyRun == 0;
        }
        if (!Bot(leader))
            return false;
        bool exactGroup = leader->GetGroup() && leader->GetGroup()->GetGUID().GetCounter() == p.group &&
                          leader->GetGroup()->GetLeaderGUID() == leader->GetGUID() &&
                          leader->GetGroup()->GetMembersCount() == 5;
        for (auto guid : p.members)
        {
            auto* member = Find(guid);
            exactGroup = exactGroup && Bot(member) && member->GetGroup() == leader->GetGroup() &&
                         (!DcRun::Of(GET_PLAYERBOT_AI(member)).enabled || member == leader);
        }
        // Natural completion/failure has ALREADY used the normal teardown.
        // Repeating it here could release state owned by a later manual run.
        if (run.enabled)
        {
            DcActionShared::DisableDungeonClear(ai, "Autonomous operation stopped.");
            if (auto* movement = leader->GetMotionMaster())
                movement->Clear();
            leader->StopMovingOnCurrentPos();
        }
        if (exactGroup)
            for (std::size_t i = 0; i < p.members.size(); ++i)
                if (addedAoe[i])
                    GET_PLAYERBOT_AI(Find(p.members[i]))->ChangeStrategy("-avoid aoe", BOT_STATE_COMBAT);
        run.autonomyRun = 0;
        ActiveAutonomy.store(false, std::memory_order_release);
        return !run.enabled;
    }
} engine;
Controller controller(engine);
Status Start(Permit const& p) { return controller.Start(p); }
Status Poll(Permit const& p) { return controller.Poll(p); }
Status Stop(Permit const& p) { return controller.Stop(p); }
Executor const executor{Version, Start, Poll, Stop};
class BridgeWorld final : public WorldScript
{
public:
    BridgeWorld() : WorldScript("DcAutonomyBridge", {WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE}) {}
    void OnStartup() override
    {
        engine.thread = std::this_thread::get_id();
        RegisterExecutor(&executor);
    }
    void OnUpdate(uint32 diff) override
    {
        if (diff < _checkIn)
        {
            _checkIn -= diff;
            return;
        }
        _checkIn = 1000;
        controller.Tick();
    }

private:
    uint32 _checkIn = 0;
};
} // namespace
} // namespace DcAutonomy
void AddSC_dungeon_clear_autonomy() { new DcAutonomy::BridgeWorld(); }
