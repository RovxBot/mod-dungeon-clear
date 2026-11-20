#ifndef DC_AUTONOMY_API_H
#define DC_AUTONOMY_API_H

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include "DcAutonomyPilot.h"

// Internal, value-only ABI. Calls are restricted to the world update barrier
// after map workers have joined. Registration is process-lifetime, single owner.
namespace DcAutonomy
{
inline constexpr std::uint32_t Version = 2;
struct Permit
{
    std::uint32_t version = Version;
    std::uint32_t profileVersion = 0, profile = 0, realmExpansion = 0;
    std::uint64_t owner = 0, operation = 0, revision = 0, nonce = 0, expires = 0;
    std::uint32_t group = 0, leader = 0, map = 0, instance = 0, difficulty = 0;
    std::array<std::uint32_t, 5> members{};
};
inline bool Same(Permit const& a, Permit const& b)
{
    return a.version == b.version && a.profileVersion == b.profileVersion && a.profile == b.profile &&
           a.realmExpansion == b.realmExpansion && a.owner == b.owner && a.operation == b.operation && a.revision == b.revision &&
           a.nonce == b.nonce && a.expires == b.expires && a.group == b.group && a.leader == b.leader &&
           a.map == b.map && a.instance == b.instance && a.difficulty == b.difficulty && a.members == b.members;
}
enum class State : std::uint8_t
{
    NotStarted,
    Starting,
    Running,
    Paused,
    Completed,
    Failed,
    Stopped
};
inline char const* StateName(State s)
{
    switch (s)
    {
    case State::NotStarted:
        return "not_started";
    case State::Starting:
        return "starting";
    case State::Running:
        return "running";
    case State::Paused:
        return "paused";
    case State::Completed:
        return "completed";
    case State::Failed:
        return "failed";
    case State::Stopped:
        return "stopped";
    }
    return "unknown";
}
inline bool Terminal(State s) { return s == State::Completed || s == State::Failed || s == State::Stopped; }
struct Status
{
    bool quiescent = false;
    State state = State::NotStarted;
    std::uint64_t operation = 0, run = 0, lastProgress = 0;
    std::uint32_t leader = 0, group = 0, map = 0, instance = 0, encounters = 0, objectives = 0;
    std::uint32_t requiredEncounters = 0;
    std::string reason = "adapter_unavailable";
};
inline bool Matches(Status const& s, Permit const& p)
{
    return s.operation == p.operation && s.run == p.nonce && s.leader == p.leader && s.group == p.group &&
           s.map == p.map && s.instance == p.instance;
}
struct Owner
{
    std::uint32_t version = Version;
    // Must verify its own exact pending permit, durable leases, M1 and current
    // identities. consume=true atomically burns the permit before any mutation.
    bool (*authorize)(Permit const&, bool consume) = nullptr;
};
struct Executor
{
    std::uint32_t version = Version;
    Status (*start)(Permit const&) = nullptr;
    Status (*status)(Permit const&) = nullptr;
    Status (*stop)(Permit const&) = nullptr;
};
inline std::atomic<Owner const*> OwnerProvider{nullptr};
inline std::atomic<Executor const*> ExecutorProvider{nullptr};
template <class T> bool Register(std::atomic<T const*>& slot, T const* provider)
{
    if (!provider || provider->version != Version)
        return false;
    T const* expected = nullptr;
    return slot.compare_exchange_strong(expected, provider) || expected == provider;
}
inline bool RegisterOwner(Owner const* p) { return p && p->authorize && Register(OwnerProvider, p); }
inline bool RegisterExecutor(Executor const* p)
{
    return p && p->start && p->status && p->stop && Register(ExecutorProvider, p);
}
inline Executor const* GetExecutor()
{
    auto* p = ExecutorProvider.load(std::memory_order_acquire);
    return p && p->version == Version && p->start && p->status && p->stop ? p : nullptr;
}
} // namespace DcAutonomy
#endif
