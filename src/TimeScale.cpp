#include "TimeScale.h"

namespace ProjectileControl
{
	void TimeScale::Write(Scale a_scale)
	{
		auto* vats = RE::VATS::GetSingleton();
		if (!vats) {
			return;
		}
		// While the game's own slowdown (Slow Time) is running, the ride never makes it
		// weaker: of the two, the stronger one wins.
		Scale out = a_scale;
		if (_game.world > 0.f) {
			out.world = (std::min)(out.world, _game.world);
			if (_game.player > 0.f) {
				out.player = (std::min)(out.player, _game.player);
			}
		}
		vats->SetMagicTimeSlowdown(out.world, out.player);
		_written = out;
	}

	void TimeScale::Apply(float a_worldScale, float a_playerScale)
	{
		_wanted = { a_worldScale, a_playerScale };

		auto* vats = RE::VATS::GetSingleton();
		if (!vats) {
			return;
		}
		if (!_applied) {
			// Whatever the game had going before the ride -- usually nothing (0), or the
			// Slow Time shout -- is what Clear puts back.
			_game = { vats->magicTimeSlowdown, vats->playerMagicTimeSlowdown };
			_applied = true;
		} else {
			AdoptForeignWrite();
		}
		Write(_wanted);
	}

	void TimeScale::AdoptForeignWrite()
	{
		auto* vats = RE::VATS::GetSingleton();
		// Someone else wrote the field since our last write -- the Slow Time shout starting
		// (its magnitude) or ending ((0, 1)), or another mod. Whatever it was is now the
		// game's state and what Clear hands back: keeping the value from before the ride
		// would leave a shout that ended mid-ride running forever.
		if (vats && (vats->magicTimeSlowdown != _written.world || vats->playerMagicTimeSlowdown != _written.player)) {
			_game = { vats->magicTimeSlowdown, vats->playerMagicTimeSlowdown };
		}
	}

	void TimeScale::Reapply()
	{
		if (!_applied) {
			return;
		}
		AdoptForeignWrite();
		Write(_wanted);
	}

	void TimeScale::Clear()
	{
		if (!_applied) {
			return;
		}
		_applied = false;

		auto* vats = RE::VATS::GetSingleton();
		if (!vats) {
			return;
		}
		// A shout still running gets its own value back. Otherwise zero: the engine keeps
		// magicTimeSlowdown as an "effect is running" flag, and any positive value leaves the
		// player stuck in the slowdown (mounting a horse and other actions stay blocked). A
		// zero also resets the global time multiplier to 1.
		if (_game.world > 0.f) {
			vats->SetMagicTimeSlowdown(_game.world, _game.player);
		} else {
			vats->SetMagicTimeSlowdown(0.f, _game.player > 0.f ? _game.player : 1.f);
		}
		_game = {};
		_written = {};
	}
}
