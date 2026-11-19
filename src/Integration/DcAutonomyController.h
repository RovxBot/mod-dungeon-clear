#ifndef DC_AUTONOMY_CONTROLLER_H
#define DC_AUTONOMY_CONTROLLER_H

#include "DcAutonomyApi.h"
#include <algorithm>
#include <set>

namespace DcAutonomy
{
inline bool PilotCompleted(bool allClearedSignal, std::uint32_t encounterMask)
{
    return allClearedSignal && (encounterMask & 7u) == 7u;
}
// One pilot operation, bounded memory. Engine adapter supplies authoritative
// evidence; this same controller is executed by the deterministic tests.
class Port
{
public:
    virtual ~Port() = default;
    virtual bool OnWorldThread() const = 0;
    virtual bool Enabled() const = 0;
    virtual std::uint64_t Now() const = 0;
    virtual std::string Validate(Permit const&) const = 0;
    virtual bool Initialize(Permit const&) = 0;
    virtual Status Observe(Permit const&) = 0;
    virtual bool Teardown(Permit const&) = 0;
};
class Controller
{
public:
    explicit Controller(Port& port) : _port(port) {}
    Status Start(Permit const& p)
    {
        if (!_port.OnWorldThread())
            return Refuse(p, "wrong_world_thread");
        if (!_port.Enabled())
            return Refuse(p, "trusted_bridge_disabled");
        auto* owner = OwnerProvider.load(std::memory_order_acquire);
        if (!owner || owner->version != Version || !owner->authorize)
            return Refuse(p, "owner_provider_missing");
        auto now = _port.Now();
        std::set<std::uint32_t> unique(p.members.begin(), p.members.end());
        if (p.version != Version || !p.owner || !p.operation || !p.nonce || !p.revision || !p.group || !p.leader ||
            !p.instance || p.map != 574 || p.difficulty != 0 || p.expires <= now || p.expires - now > 15 ||
            unique.size() != 5 || unique.count(0) || !unique.count(p.leader))
            return Refuse(p, "invalid_or_expired_permit");
        if (_held && !_status.quiescent)
            return Refuse(p, "run_already_owned");
        // The owner remembers consumed permits; the executor never accepts an
        // operation ID, nonce or roster as authority on its own.
        if (!owner->authorize(p, false))
            return Refuse(p, "permit_rejected_by_owner");
        auto rejection = _port.Validate(p);
        if (!rejection.empty())
            return Refuse(p, rejection);
        if (!owner->authorize(p, true))
            return Refuse(p, "permit_replayed_or_revoked");
        _permit = p;
        _held = true;
        _status = Identity(p);
        _status.state = State::Starting;
        _status.lastProgress = now;
        _status.reason = "starting";
        if (!_port.Initialize(p))
        {
            _status.state = State::Failed;
            _status.reason = "shared_initialization_failed";
            return _status;
        }
        return Poll(p);
    }
    Status Poll(Permit const& p)
    {
        if (!_port.OnWorldThread())
            return Refuse(p, "wrong_world_thread");
        if (!_held || !Same(p, _permit))
            return Refuse(p, "run_identity_mismatch");
        if (Terminal(_status.state))
        {
            if (!_status.quiescent)
                _status.quiescent = _port.Teardown(p);
            return _status;
        }
        _status = _port.Observe(p);
        _status.reason.resize(std::min<std::size_t>(_status.reason.size(), 160));
        if (Terminal(_status.state))
            _status.quiescent = _port.Teardown(p);
        return _status;
    }
    Status Stop(Permit const& p)
    {
        if (!_port.OnWorldThread())
            return Refuse(p, "wrong_world_thread");
        if (!_held || !Same(p, _permit))
            return Refuse(p, "run_identity_mismatch");
        if (_status.quiescent)
            return _status;
        if (!_port.Teardown(p))
        {
            _status.reason = "stop_pending_identity_or_logout";
            return _status;
        }
        if (!Terminal(_status.state))
        {
            _status.state = State::Stopped;
            _status.reason = "authorised_stop";
        }
        _status.quiescent = true;
        return _status;
    }
    // Called on every world update even when the feature is disabled. No SQL.
    void Tick()
    {
        if (!_held || _status.quiescent || !_port.OnWorldThread())
            return;
        auto* owner = OwnerProvider.load(std::memory_order_acquire);
        if (!_port.Enabled() || !owner || !owner->authorize || !owner->authorize(_permit, false) ||
            !_port.Validate(_permit).empty())
            Stop(_permit);
        else
            Poll(_permit);
    }
    static Status Identity(Permit const& p)
    {
        Status s;
        s.operation = p.operation;
        s.run = p.nonce;
        s.leader = p.leader;
        s.group = p.group;
        s.map = p.map;
        s.instance = p.instance;
        return s;
    }

private:
    static Status Refuse(Permit const& p, std::string reason)
    {
        auto s = Identity(p);
        s.reason = std::move(reason);
        return s;
    }
    Port& _port;
    bool _held = false;
    Permit _permit;
    Status _status;
};
} // namespace DcAutonomy
#endif
