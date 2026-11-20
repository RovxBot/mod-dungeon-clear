#include "Integration/DcAutonomyController.h"
#include <cassert>
#include <chrono>
#include <iostream>

using namespace DcAutonomy;
namespace
{
Permit issued;
bool consumed = false, valid = true;
bool Authorize(Permit const& p, bool consume)
{
    if (!valid || !Same(p, issued) || (consume && consumed))
        return false;
    if (consume)
        consumed = true;
    return true;
}
struct Fake final : Port
{
    bool world = true, enabled = true, initialize = true, stop = true;
    std::uint64_t now = 100;
    unsigned starts = 0, stops = 0;
    unsigned mask = 15, required = 15;
    bool mismatch = false;
    std::string rejection;
    State state = State::Running;
    bool OnWorldThread() const override { return world; }
    bool Enabled() const override { return enabled; }
    std::uint64_t Now() const override { return now; }
    std::string Validate(Permit const&) const override { return rejection; }
    bool Initialize(Permit const&) override
    {
        ++starts;
        return initialize;
    }
    Status Observe(Permit const& p) override
    {
        auto s = Controller::Identity(p);
        s.state = state;
        s.reason = "actual_engine_observation";
        s.lastProgress = now;
        s.encounters = mask;
        s.requiredEncounters = required;
        if (mismatch)
            ++s.instance;
        return s;
    }
    bool Teardown(Permit const&) override
    {
        ++stops;
        return stop;
    }
};
Permit Make()
{
    Permit p;
    p.owner = 11;
    p.operation = 12;
    p.revision = 9;
    p.nonce = 13;
    p.expires = 110;
    p.group = 20;
    p.leader = 1;
    p.map = 389;
    p.profileVersion = PilotVersion;
    p.profile = RagefirePilot;
    p.realmExpansion = 1;
    p.instance = 30;
    p.members = {1, 2, 3, 4, 5};
    issued = p;
    consumed = false;
    valid = true;
    return p;
}
} // namespace
int main()
{
    for (unsigned mask = 0; mask < 15; ++mask)
        assert(!PilotCompleted(true, mask, 15));
    assert(!PilotCompleted(false, 15, 15) && PilotCompleted(true, 15, 15));
    assert(!PilotCompleted(true, 7, 7) && !PilotCompleted(true, 15, 0));
    assert(PilotCompleted(true, 0x80000015u, 0x80000015u));
    assert(FindPilot(PilotVersion, RagefirePilot) == &Ragefire);
    assert(!FindPilot(0, 1) && !FindPilot(PilotVersion, 0) && !FindPilot(PilotVersion, 2));
    for (auto cap : {0u, 1u})
    {
        assert(PilotAllowed(PilotVersion, RagefirePilot, 389, 0, cap));
        assert(!PilotAllowed(PilotVersion, RagefirePilot, 574, 0, cap));
        assert(!PilotAllowed(PilotVersion, RagefirePilot, 34, 0, cap));
        assert(!PilotAllowed(PilotVersion, RagefirePilot, 389, 1, cap));
        assert(PilotEligible(Ragefire, cap, 15, 13));
        assert(!PilotEligible(Ragefire, cap, 14, 13));
        assert(!PilotEligible(Ragefire, cap, 15, 12));
        assert(PilotEligible(Ragefire, cap, PilotMaxLevel(cap), 115));
        assert(!PilotEligible(Ragefire, cap, PilotMaxLevel(cap) + 1, 115));
    }
    assert(!PilotAllowed(PilotVersion, RagefirePilot, 389, 0, 2));
    assert(!PilotEligible(Ragefire, 2, 80, 200));
    Executor incompatible{1, nullptr, nullptr, nullptr};
    assert(!RegisterExecutor(&incompatible));
    ExecutorProvider = &incompatible;
    assert(!GetExecutor());
    ExecutorProvider = nullptr;
    assert(!GetExecutor());
    Owner const owner{Version, Authorize};
    OwnerProvider = nullptr;
    {
        Fake f;
        Controller c(f);
        assert(c.Start(Make()).reason == "owner_provider_missing");
    }
    assert(RegisterOwner(&owner));
    Owner other{Version, Authorize};
    assert(!RegisterOwner(&other));
    for (unsigned fault = 0; fault < 20; ++fault)
    {
        Fake f;
        Controller c(f);
        auto p = Make();
        switch (fault)
        {
        case 0:
            p.expires = 100;
            break;
        case 1:
            p.expires = 116;
            break;
        case 2:
            p.version = 1;
            break;
        case 3:
            p.owner = 99;
            break;
        case 4:
            p.nonce = 99;
            break;
        case 5:
            p.members[4] = 4;
            break;
        case 6:
            p.members[4] = 0;
            break;
        case 7:
            p.map = 33;
            break;
        case 8:
            p.instance = 0;
            break;
        case 9:
            p.difficulty = 1;
            break;
        case 10:
            p.group = 99;
            break;
        case 11:
            p.leader = 4;
            break;
        case 12:
            f.enabled = false;
            break;
        case 13:
            f.world = false;
            break;
        case 14:
            valid = false;
            break;
        case 16:
            p.profile = 0;
            break;
        case 17:
            p.profileVersion = 2;
            break;
        case 18:
            p.realmExpansion = 2;
            break;
        case 19:
            p.map = 574;
            break;
        case 15:
            f.rejection = "wrong_authoritative_roster_or_control";
            break;
        }
        assert(c.Start(p).state == State::NotStarted && f.starts == 0 && !consumed);
    }
    for (auto state : {State::Starting, State::Running, State::Paused, State::Completed, State::Failed})
    {
        Fake f;
        Controller c(f);
        auto p = Make();
        f.state = state;
        auto s = c.Start(p);
        assert(s.state == state && s.operation == p.operation && s.run == p.nonce && consumed && f.starts == 1);
        assert(c.Start(p).state == State::NotStarted && f.starts == 1);
        auto wrong = p;
        ++wrong.group;
        assert(c.Stop(wrong).reason == "run_identity_mismatch" && f.stops <= 1);
        auto stopped = c.Stop(p);
        assert(stopped.quiescent && (Terminal(state) ? stopped.state == state : stopped.state == State::Stopped));
        auto count = f.stops;
        assert(c.Stop(p).quiescent && f.stops == count);
    }
    for (unsigned mask = 0; mask < 15; ++mask)
    {
        Fake f;
        Controller c(f);
        f.state = State::Completed;
        f.mask = mask;
        auto s = c.Start(Make());
        assert(s.state == State::Failed && s.quiescent && f.stops == 1);
    }
    {
        Fake f;
        Controller c(f);
        f.mismatch = true;
        assert(c.Start(Make()).reason == "executor_observation_identity_mismatch");
    }
    {
        Fake f;
        Controller c(f);
        auto p = Make();
        c.Start(p);
        f.stop = false;
        assert(!c.Stop(p).quiescent);
        f.stop = true;
        assert(c.Stop(p).quiescent);
    }
    for (unsigned fault = 0; fault < 3; ++fault)
    {
        Fake f;
        Controller c(f);
        auto p = Make();
        c.Start(p);
        if (!fault)
            f.enabled = false;
        else if (fault == 1)
            valid = false;
        else
            f.rejection = "logout_leader_change_or_human_join";
        c.Tick();
        assert(c.Poll(p).quiescent && f.stops == 1);
    }
    {
        Fake f;
        Controller c(f);
        auto p = Make();
        f.initialize = false;
        assert(c.Start(p).state == State::Failed && consumed);
        assert(c.Stop(p).quiescent);
    }
    auto started = std::chrono::steady_clock::now();
    Fake f;
    Controller c(f);
    auto p = Make();
    c.Start(p);
    for (unsigned n = 0; n < 100000; ++n)
        c.Tick();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
    std::cout << "PASS production bridge controller: forged/expired/replay/mismatch, status, stop/startup/terminal, "
                 "disable/logout/handoff; 100000 fake ticks_us="
              << us << " SQL=0 (engine fakes, no server)\n";
}
