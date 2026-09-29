#pragma once

namespace ProjectileControl
{
	class SettingsMenu;

	// Data/SKSE/Plugins/STB_ProjectileControl.ini, read at kDataLoaded. With SKSE Menu Framework
	// installed the same values are edited live from its page (SettingsMenu) and written
	// back to the INI.
	//
	// Every value lives in one Values struct whose member initialisers are the defaults,
	// so "Reset to defaults" is a plain assignment and there is no second copy of them to
	// drift. Getters read it every time -- nothing downstream caches a setting, which is
	// what makes the menu's changes apply on the next frame.
	class Settings
	{
	public:
		// Which pool pays for the ride. kNone switches the whole cost off.
		enum class CostPool
		{
			kNone,
			kStamina,
			kMagicka,
			kHealth
		};

		// What one ride costs. Arrows and spells carry their own, so a bow can cost stamina
		// while a spell costs magicka.
		struct Cost
		{
			CostPool pool = CostPool::kStamina;
			// Paid once as the ride starts.
			float start = 0.f;
			// Paid per REAL second while the projectile is steered (not during the linger).
			float perSecond = 0.f;
			// On: no ride without the full start cost, and the ride ends when the pool runs
			// dry. Off: the ride always happens and simply drains the pool to zero.
			bool cancelOnShortage = true;
		};

		struct Values
		{
			// ---- keys ----
			std::uint32_t triggerKeyboard = 0x2D;  // X, unbound in vanilla
			std::uint32_t triggerGamepad = 0x0100;  // LB (sprint -- nothing to sprint with a bow drawn)
			std::uint32_t cancelKeyboard = 0x39;    // Space -- jumping is off during a ride
			std::uint32_t cancelGamepad = 0x8000;   // Y -- jump, off during a ride; B would open the menu
			std::uint32_t mouseSteerKey = 0x100;   // left mouse button (0x100 + button index)
			std::uint32_t slowerKey = 0x0C;        // -
			std::uint32_t fasterKey = 0x0D;        // = (the + key)

			// ---- projectiles ----
			bool        arrows = true;
			bool        bolts = true;
			bool        spells = true;
			std::string alwaysRide;  // raw INI text, resolved into _alwaysRide by Apply

			// ---- hint ----
			bool  showHint = true;
			bool  hintShowSteer = true;
			float hintScale = 100.f;
			float hintFontSize = 20.f;
			float hintBottom = 70.f;

			// ---- camera ----
			float distance = 100.f;
			float height = 20.f;
			float followLag = 0.15f;
			float bankAngle = 15.f;
			float bankLag = 0.2f;
			bool  mouseLook = false;
			float mouseSensitivity = 0.35f;
			bool  invertMouseY = false;
			float mousePitchLimit = 70.f;
			float recenterSpeed = 0.f;
			float recenterDelay = 1.f;

			// ---- impact ----
			float impactLinger = 1.f;
			bool  lingerEaseTime = true;

			// ---- steering ----
			float turnRate = 75.f;
			float turnResponse = 8.f;
			bool  invertSteerY = false;
			float pitchLimit = 85.f;
			bool  allowLoops = true;
			float mouseSteerSensitivity = 0.04f;
			float gravityScale = 1.f;
			float speedScale = 1.f;

			// ---- cost ----
			Cost arrowCost{ .pool = CostPool::kStamina, .start = 10.f, .perSecond = 1.f };
			Cost spellCost{ .pool = CostPool::kMagicka, .start = 10.f, .perSecond = 1.f };

			// ---- requirements (raw INI text, resolved by Apply) ----
			std::string arrowPerk;
			std::string spellPerk;

			// ---- time ----
			float worldScale = 0.25f;
			float playerScale = 0.25f;

			// ---- range ----
			float maxDistance = 0.f;
			float lifeScale = 5.f;

			// ---- general ----
			float maxDuration = 60.f;
			bool  debugLog = false;
		};

		// kDataLoaded: read the INI, then Apply.
		static void Load();

		// Re-clamps every value and, unless told not to, re-resolves the perks and the
		// always-ride list from their text. Main thread only: the list is read by the
		// projectile hook.
		static void Apply(bool a_resolveForms = true);

		// Writes every value back into the INI. The file is read first and only the values
		// are replaced, so its comments survive.
		static void Save();

		// Bumped whenever a value changes at runtime; the HUD hint re-lays itself out on it.
		static std::uint32_t Generation() { return _generation.load(std::memory_order_relaxed); }
		static void          Touch() { _generation.fetch_add(1, std::memory_order_relaxed); }

		// ---- keys ----
		// DirectInput scancode held while the bow string is released. 0 = keyboard trigger
		// disabled.
		static std::uint32_t TriggerKeyboard() { return _v.triggerKeyboard; }
		// Gamepad button mask as the game reports it. 0 = disabled.
		static std::uint32_t TriggerGamepad() { return _v.triggerGamepad; }

		// Ends the ride at once, linger included; the arrow flies on by itself. Reacts to
		// the press, not to holding, so it may even be the trigger key.
		static std::uint32_t CancelKeyboard() { return _v.cancelKeyboard; }
		static std::uint32_t CancelGamepad() { return _v.cancelGamepad; }

		// Held during a ride, the mouse steers the projectile instead of swinging the
		// camera. Keyboard scancode, or 0x100 + mouse button (0x100 = left, 0x101 = right,
		// 0x102 = middle) -- the same numbering as the keycap art. 0 = off.
		static std::uint32_t MouseSteerKey() { return _v.mouseSteerKey; }
		static constexpr std::uint32_t kMouseBase = 0x100;

		// One step of slow motion per press during a ride: slower / faster. The new value is
		// kept (see SetTimeScale). 0 = off.
		static std::uint32_t SlowerKey() { return _v.slowerKey; }
		static std::uint32_t FasterKey() { return _v.fasterKey; }

		// ---- hint ----
		// Keycaps and labels at the bottom of the HUD while an arrow is being flown.
		static bool  ShowHint() { return _v.showHint; }
		static bool  HintShowSteer() { return _v.hintShowSteer; }
		static float HintScale() { return _v.hintScale; }
		static float HintFontSize() { return _v.hintFontSize; }
		// Distance of the row from the bottom of the visible screen, in HUD units.
		static float HintBottom() { return _v.hintBottom; }

		// ---- projectiles ----
		// Which of the player's projectiles the trigger can take. Arrows and bolts are both
		// ArrowProjectile, told apart by the weapon that fired them. Spells are
		// MissileProjectile: fire-and-forget bolts, balls and spikes from hands, staves and
		// scrolls. Beams and flames (concentration) have nothing that flies, and cones
		// (Unrelenting Force and the like) are a short-lived expanding volume -- neither
		// can be steered, so neither is hooked.
		static bool Arrows() { return _v.arrows; }
		static bool Bolts() { return _v.bolts; }
		static bool Spells() { return _v.spells; }

		// Forms that start a ride on their own, with no trigger key held: a spell, a piece
		// of ammo, a weapon or a projectile form. Matched against all four sources of the
		// shot, so any of them can be listed. Empty (the default) = the key is the only way
		// in. A listed form rides even when its kind is switched off above -- that is the
		// point of naming it.
		static bool AlwaysRides(RE::FormID a_form)
		{
			return !_alwaysRide.empty() && _alwaysRide.contains(a_form);
		}
		static bool HasAlwaysList() { return !_alwaysRide.empty(); }

		// ---- camera ----
		static float Distance() { return _v.distance; }
		static float Height() { return _v.height; }
		// Seconds the camera's heading trails the arrow's (critically damped spring).
		// 0 = locked rigidly behind the arrow.
		static float FollowLag() { return _v.followLag; }
		// Maximum roll into a turn, degrees. 0 = off, negative flips the direction.
		static float BankAngle() { return _v.bankAngle; }
		static float BankLag() { return _v.bankLag; }
		static bool  MouseLook() { return _v.mouseLook; }
		static float MouseSensitivity() { return _v.mouseSensitivity; }
		static bool  InvertMouseY() { return _v.invertMouseY; }
		static float MousePitchLimit() { return _v.mousePitchLimit; }
		static float RecenterSpeed() { return _v.recenterSpeed; }
		// Seconds the mouse has to rest before recentering starts. Without it the drift
		// fights the hand that is still turning the camera.
		static float RecenterDelay() { return _v.recenterDelay; }

		// ---- impact ----
		// Real seconds the camera stays on the arrow after it hits, before handing back.
		// 0 cuts back on the impact frame.
		static float ImpactLinger() { return _v.impactLinger; }
		// During that pause, ease time from the ride's slow motion back to normal, so the
		// hit is seen slowed and the target's reaction at speed.
		static bool LingerEaseTime() { return _v.lingerEaseTime; }

		// ---- steering ----
		// Maximum turn rate, degrees per real second.
		static float TurnRate() { return _v.turnRate; }
		// How fast the turn rate builds up and dies down, 1/s. Higher is snappier; 0 makes
		// the keys switch the full rate on and off instantly.
		static float TurnResponse() { return _v.turnResponse; }
		static bool  InvertSteerY() { return _v.invertSteerY; }
		static float PitchLimit() { return _v.pitchLimit; }
		// Pitch is not limited: holding W carries the projectile over the top into a full
		// loop, the camera turning upside down with it.
		static bool  AllowLoops() { return _v.allowLoops; }
		// Degrees of turn per unit of mouse movement while the mouse steers.
		static float MouseSteerSensitivity() { return _v.mouseSteerSensitivity; }
		static float GravityScale() { return _v.gravityScale; }
		static float SpeedScale() { return _v.speedScale; }

		// ---- cost ----
		static const Cost& ArrowCost() { return _v.arrowCost; }
		static const Cost& SpellCost() { return _v.spellCost; }

		// ---- requirements ----
		// Perk the player must have for the trigger to work, or nullptr for none; separate
		// for arrows and for spells.
		static RE::BGSPerk* ArrowPerk() { return _arrowPerk; }
		static RE::BGSPerk* SpellPerk() { return _spellPerk; }

		// ---- time ----
		static float WorldScale() { return _v.worldScale; }
		static float PlayerScale() { return _v.playerScale; }
		// The slow-motion keys during a ride. Main thread; the caller saves the INI.
		static void SetTimeScale(float a_world, float a_player);

		// ---- range ----
		// Game units from where the shot was fired; the ride hands back past it. 0 = no
		// cap. A shot leaving the loaded part of the world ends the ride regardless.
		static float MaxDistance() { return _v.maxDistance; }
		// Multiplies how far the projectile may fly before the engine removes it (its
		// range) for as long as it is being steered. 0 = it never runs out.
		static float LifeScale() { return _v.lifeScale; }

		// ---- general ----
		// Safety backstop, in real seconds: the session is torn down even if the arrow
		// never reported its death. Not a gameplay timer -- keep it generous.
		static float MaxDuration() { return _v.maxDuration; }
		static bool  DebugLog() { return _v.debugLog; }

	private:
		// The menu edits _v in place; see SettingsMenu for which fields it may write from
		// the render thread and which go through a main-thread task.
		friend class SettingsMenu;

		static void Clamp();
		static void ResolveForms();

		static inline Values _v;

		// Resolved from _v.alwaysRide / _v.arrowPerk / _v.spellPerk by ResolveForms.
		static inline std::unordered_set<RE::FormID> _alwaysRide;
		static inline RE::BGSPerk*                   _arrowPerk = nullptr;
		static inline RE::BGSPerk*                   _spellPerk = nullptr;

		static inline std::atomic<std::uint32_t> _generation{ 0 };
	};
}
