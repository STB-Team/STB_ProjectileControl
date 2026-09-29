#pragma once

namespace ProjectileControl
{
	// Slow motion for the duration of a ride: the game's own magic slowdown,
	// VATS::SetMagicTimeSlowdown -- the setter the Slow Time shout uses. See Apply for how a
	// ride shares it with the shout.
	class TimeScale
	{
	public:
		// May be called every frame (the linger eases the scale back); the first call of a
		// ride records what the game had set on its own.
		static void Apply(float a_worldScale, float a_playerScale);

		// Once per frame during a ride: notices when the game changed the slowdown on its
		// own meanwhile, adopts that as what to hand back, and re-asserts the ride's value.
		static void Reapply();

		// Hands back whatever the game itself last set.
		static void Clear();

	private:
		struct Scale
		{
			float world = 0.f;
			float player = 0.f;
		};

		static void Write(Scale a_scale);
		static void AdoptForeignWrite();

		static inline bool  _applied = false;
		static inline Scale _wanted;   // what the ride asks for
		static inline Scale _written;  // what was last put into the engine
		static inline Scale _game;     // what the game had set on its own, to hand back
	};
}
