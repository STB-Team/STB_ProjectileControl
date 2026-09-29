#pragma once

namespace ProjectileControl
{
	// Raw input sink. It only ever writes atomics: it runs on the input thread, while every
	// camera, projectile and time-scale change belongs to the main thread.
	//
	// Movement keys are read as raw scancodes matched against the player's own bindings
	// (resolved from the gameplay context once per session), not through PlayerControls:
	// while the arrow is being flown the free camera owns the movement handler and the
	// control map has fighting/activate switched off, so nothing else would deliver them.
	class Input : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static Input* GetSingleton();
		static void   Register();

		RE::BSEventNotifyControl ProcessEvent(
			RE::InputEvent* const*               a_event,
			RE::BSTEventSource<RE::InputEvent*>* a_source) override;

		// Trigger modifier state at this instant. Read on the arrow's first update.
		static bool TriggerHeld() { return _triggerHeld.load(std::memory_order_relaxed); }

		// -1..1 steering axes. X = yaw (A/D), Y = pitch (W/S).
		static float SteerX() { return _steerX.load(std::memory_order_relaxed); }
		static float SteerY() { return _steerY.load(std::memory_order_relaxed); }

		// Mouse movement accumulated since the last call, in raw device units.
		static void ConsumeMouse(float& a_dx, float& a_dy);

		// Right stick, -1..1: looks around like the mouse does. Held, not accumulated.
		static float LookX() { return _lookX.load(std::memory_order_relaxed); }
		static float LookY() { return _lookY.load(std::memory_order_relaxed); }

		// The mouse-steer key is held right now: the mouse flies the projectile.
		static bool MouseSteerHeld() { return _mouseSteerHeld.load(std::memory_order_relaxed); }

		// Presses of the slow-motion keys since the last call: faster minus slower.
		static int ConsumeTimeSteps() { return _timeSteps.exchange(0, std::memory_order_relaxed); }

		// True once per press of the cancel key during a ride. The press is latched here
		// and acted on by the main thread.
		static bool ConsumeCancel() { return _cancelPressed.exchange(false, std::memory_order_relaxed); }

		// Scancodes of Forward, Back, Strafe Left, Strafe Right as bound by the player,
		// resolved at the start of the ride. 0 = unbound.
		static std::uint32_t MoveKey(std::size_t a_index)
		{
			return a_index < 4 ? _moveKeys[a_index].load(std::memory_order_relaxed) : 0;
		}

		// The device the cancel key was last pressed on, so the hint can show the
		// gamepad button to a player using a gamepad.
		static bool UsingGamepad() { return _usingGamepad.load(std::memory_order_relaxed); }

		// Whether the game's attack/block handler must not see input now: for the whole
		// ride, and after it until every attack button has been let go. Holding the mouse
		// to steer is otherwise remembered by that handler and fired the moment control
		// comes back. Called from the handler hook.
		static bool AttackBlocked();

		// Re-reads the player's movement bindings and drops any stale axis state.
		static void BeginSession();
		static void EndSession();

	private:
		Input() = default;
		Input(const Input&) = delete;
		Input& operator=(const Input&) = delete;

		static void UpdateAxes();

		static inline std::atomic<bool>  _triggerHeld{ false };
		static inline std::atomic<float> _steerX{ 0.f };
		static inline std::atomic<float> _steerY{ 0.f };
		static inline std::atomic<float> _mouseDX{ 0.f };
		static inline std::atomic<float> _mouseDY{ 0.f };
		static inline std::atomic<float> _lookX{ 0.f };
		static inline std::atomic<float> _lookY{ 0.f };
		static inline std::atomic<bool>  _mouseSteerHeld{ false };
		// Held state of the buttons that attack or cast: left/right mouse, left/right trigger.
		static inline std::atomic<bool>  _attackHeld[4]{};
		// Set at the end of a ride while an attack button is still down.
		static inline std::atomic<bool>  _blockAfterRide{ false };
		static inline std::atomic<int>   _timeSteps{ 0 };

		// Held state of the four movement keys, index matches _moveKeys.
		static inline std::atomic<bool>          _moveDown[4]{};
		static inline std::atomic<std::uint32_t> _moveKeys[4]{};
		static inline std::atomic<bool>          _sessionActive{ false };
		static inline std::atomic<bool>          _cancelPressed{ false };
		static inline std::atomic<bool>          _usingGamepad{ false };
	};
}
