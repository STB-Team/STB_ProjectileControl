#pragma once

namespace ProjectileControl
{
	// One ride: the arrow that is being flown, the camera that follows it, the slow motion
	// and the controls that were switched off for it.
	//
	// Everything here runs on the main thread -- the projectile hook, the camera hook and
	// the player-update watchdog all call in from it. The input sink never touches it.
	class Session
	{
	public:
		static bool IsActive() { return _active; }

		// Whether the HUD hint should be up: while the arrow is being flown, not during the
		// linger (the hit is the shot there). Atomic because the HUD reads it from its own
		// update.
		static bool HintVisible() { return _hintVisible.load(std::memory_order_relaxed); }

		// True for the one arrow being flown. Cheap enough for a per-projectile hook.
		static bool IsTracked(const RE::Projectile* a_projectile)
		{
			return _active && a_projectile == _arrow;
		}

		// Which vtable the projectile came through: the hook knows, the object would need
		// an RTTI cast to tell.
		enum class Kind
		{
			kArrow,    // ArrowProjectile: arrows and crossbow bolts
			kMissile,  // MissileProjectile: spells, staves, scrolls
		};

		// Called from UpdateImpl for every hooked projectile while no session runs. Grabs
		// it if the trigger is held, its kind is enabled and the player shot it just now.
		static void TryCapture(RE::Projectile* a_projectile, Kind a_kind);

		// Called from ArrowProjectile::UpdateImpl for the tracked arrow, before the engine
		// integrates it: bends linearVelocity by the steering input.
		static void Steer(RE::Projectile* a_projectile);

		// Multiplies ArrowProjectile::GetGravity for the tracked arrow.
		static float GravityScale();

		// Called from the FreeCameraState getters. Rotation first, then translation --
		// that is the order FreeCameraState::Update calls them in.
		//
		// PrepareCamera runs before the original GetRotation: safety checks, then the whole
		// camera for this frame. Returns false when the ride is not drawing this frame.
		// WriteRotation runs after it and replaces the quaternion, because the original
		// hardcodes a zero roll and the camera banks into turns.
		static bool PrepareCamera(RE::FreeCameraState* a_state);
		static void WriteRotation(RE::NiQuaternion& a_rotation);
		static void ApplyCameraTranslation(RE::FreeCameraState* a_state);

		// Per-frame safety net from PlayerCharacter::Update. The same checks also run from
		// the camera hook, which is the one call that provably fires on every frame of a
		// ride -- the ride must not depend on any single hook to end.
		static void Watchdog();

		static void Stop(std::string_view a_reason);

	private:
		using Clock = std::chrono::steady_clock;

		static bool            Start(RE::Projectile* a_projectile, Kind a_kind);
		static RE::Projectile* GetProjectile();
		static void            UpdateCamera();
		static float           RealDelta(Clock::time_point& a_last);
		// Reads the projectile's real direction back into _dirYaw / _dirPitch.
		static void ReadFlight(const RE::NiPoint3& a_dir);
		// The slow-motion keys: a_steps presses, faster minus slower.
		static void AdjustTime(int a_steps);
		// Writes the INI once at the end of a ride in which the keys changed the time.
		static void SaveTimeIfChanged();
		// Gives the projectile back the range fLifeScale stretched.
		static void RestoreRange();

		// Every reason a ride can end goes through RequestStop; the teardown itself only
		// ever runs from FlushStop, on the main thread and outside the camera's own update.
		static void CheckAlive();
		// The flight is over (hit, gone, came to rest). With a linger configured the camera
		// stays for a moment to show the hit; otherwise this is RequestStop.
		static void BeginLinger(std::string_view a_reason);
		static void RequestStop(std::string_view a_reason);
		static void QueueFlush();
		static void FlushStop();

		static void BlockControls();
		static void RestoreControls();

		// Perk and start-cost gate for a fresh projectile, by its kind. Says why once per
		// projectile when the pool is short.
		static bool MayStart(RE::PlayerCharacter* a_player, RE::Projectile* a_projectile, Kind a_kind);
		// Per-second cost while steering; false when the ride has to end for it.
		static bool PayUpkeep(float a_delta);

		static inline bool                _active = false;
		static inline RE::ObjectRefHandle _handle;
		// Identity check only: never dereferenced unless _handle still resolves to it.
		static inline RE::Projectile* _arrow = nullptr;
		// Which cost rule and perk this ride answers to.
		static inline Kind _kind = Kind::kArrow;

		static inline Clock::time_point _startedAt{};
		static inline Clock::time_point _lastSeen{};
		static inline Clock::time_point _lastSteer{};
		static inline Clock::time_point _lastCamera{};

		// Flight direction of the arrow, radians. Yaw is measured from +Y clockwise,
		// pitch is positive downwards -- the engine's own convention for references. Pitch
		// is kept in the vertical plane of the heading and runs the full circle: past
		// +-90 degrees the arrow flies back along its heading, upside down -- a loop.
		static inline float _dirYaw = 0.f;
		static inline float _dirPitch = 0.f;

		// Current turn rate of the arrow, radians per real second. Eased toward the input,
		// never switched: that is what turns a digital key into an arc. Also drives the
		// camera's bank.
		static inline float _yawRate = 0.f;
		static inline float _pitchRate = 0.f;
		// Turn the mouse is putting in, radians per real second, smoothed: drives the bank
		// the way _yawRate does for the keys.
		static inline float _mouseYawRate = 0.f;

		// The slow-motion keys changed the time this ride; saved when it ends.
		static inline bool _timeChanged = false;

		// Camera heading as it trails the flight direction, plus the spring velocities.
		static inline float _followYaw = 0.f;
		static inline float _followPitch = 0.f;
		static inline float _followYawVel = 0.f;
		static inline float _followPitchVel = 0.f;

		static inline float _roll = 0.f;
		static inline float _rollVel = 0.f;

		// Mouse orbit on top of the trailing heading, radians. Not fed back into steering.
		static inline float _orbitYaw = 0.f;
		static inline float _orbitPitch = 0.f;

		static inline RE::NiPoint3 _cameraPos;
		static inline float        _cameraYaw = 0.f;
		static inline float        _cameraPitch = 0.f;

		// Last place the arrow was seen. After a hit the projectile is often killed (an
		// arrow in an actor is replaced by a model on the skeleton), so the linger frames
		// the spot where it struck rather than following a reference that is gone.
		static inline RE::NiPoint3 _lastAnchor;

		static inline Clock::time_point _lastMouseMove{};

		// After-hit pause: flight input ignored, heading frozen, camera still up.
		static inline bool              _lingering = false;
		static inline Clock::time_point _lingerStart{};
		static inline std::string_view  _lingerReason;

		// Debug self-test: the hand-built quaternion is compared with the engine's once per
		// ride, with the roll taken out.
		static inline bool _rotationChecked = false;

		static inline std::uint32_t _savedControls = 0;

		// Where the shot left from: the range cap is measured from here.
		static inline RE::NiPoint3 _shotOrigin;

		// The projectile's own range before fLifeScale touched it, put back when the ride
		// ends so a shot let go carries on as the game meant it to. -1 = not changed.
		static inline float _originalRange = -1.f;

		static inline std::atomic<bool> _hintVisible{ false };

		// Last arrow turned away for a short pool: TryCapture sees the same fresh arrow for
		// several frames, and the notification must not repeat.
		static inline const RE::Projectile* _rejectedArrow = nullptr;

		static inline bool             _stopPending = false;
		static inline bool             _flushQueued = false;
		static inline std::string_view _stopReason;
	};
}
