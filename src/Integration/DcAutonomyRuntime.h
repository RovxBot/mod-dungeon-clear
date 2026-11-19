#ifndef DC_AUTONOMY_RUNTIME_H
#define DC_AUTONOMY_RUNTIME_H
class Player;
namespace DcAutonomy
{
// Map-thread read of leader-owned context, same as existing follower signals.
bool Owns(Player* player);
} // namespace DcAutonomy
#endif
