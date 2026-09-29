#include "Session.h"

#include "Input.h"
#include "Localization.h"
#include "Settings.h"
#include "TimeScale.h"

namespace ProjectileControl
{
	namespace
	{
		constexpr float kDegToRad = std::numbers::pi_v<float> / 180.f;

		using UEFlag = RE::UserEvents::USER_EVENT_FLAG;

		// USER_EVENT_FLAG is a bare enum class with no bitwise operators, so masks are
		// assembled on the underlying type and cast back for ToggleControls.
		constexpr std::uint32_t Bit(UEFlag a_flag) { return static_cast<std::uint32_t>(a_flag); }

		// Controls switched off for the ride. Movement and looking are left alone on
		// purpose: FreeCameraState::Begin registers itself as the movement handler and
		// already swallows both. Menu and console stay on so the player can still pause.
		// Sneaking is left alone entirely -- see BlockControls for why it must be.
		constexpr std::uint32_t kBlockedControlBits = Bit(UEFlag::kFighting) | Bit(UEFlag::kActivate) |
		                                              Bit(UEFlag::kJumping) | Bit(UEFlag::kPOVSwitch) |
		                                              Bit(UEFlag::kWheelZoom);
		constexpr auto kBlockedControls = static_cast<UEFlag>(kBlockedControlBits);

		// ControlMap::ToggleControls minus the UserEventEnabled broadcast: the same mask
		// writes (enabledControls, and unk11C unless it holds kInvalid), nothing sent.
		void WriteControlsSilently(RE::ControlMap* a_controls, UEFlag a_flags, bool a_enable)
		{
			if (a_enable) {
				a_controls->enabledControls.set(a_flags);
				if (a_controls->unk11C != UEFlag::kInvalid) {
					a_controls->unk11C.set(a_flags);
				}
			} else {
				a_controls->enabledControls.reset(a_flags);
				if (a_controls->unk11C != UEFlag::kInvalid) {
					a_controls->unk11C.reset(a_flags);
				}
			}
		}

		// An arrow older than this was not shot just now: the trigger being held must not
		// pull the camera onto something already in the air.
		constexpr float kFreshArrowSeconds = 0.35f;

		// No update from the arrow for this long (real time) means the engine killed it
		// without us seeing the impact.
		constexpr float kLostArrowSeconds = 0.35f;

		// An arrow that stuck into a wall keeps its reference, keeps being updated and is
		// never flagged destroyed -- it just stops moving. Loosed arrows travel thousands
		// of units per second, so anything this slow has come to rest.
		constexpr float kStoppedSpeed = 50.f;

		// One press of a slow-motion key: multiplies the time scale, so the steps are as
		// noticeable at 0.05 as at 0.5.
		constexpr float kTimeStep = 1.25f;

		// How fast the mouse's turn feeds the bank, and how fast the camera swings back
		// behind the projectile while the mouse steers, 1/s.
		constexpr float kMouseBankResponse = 10.f;
		constexpr float kSteerRecenter = 6.f;

		// Right-stick look, degrees per real second at full tilt and default sensitivity.
		constexpr float kStickLookRate = 150.f;
		constexpr float kDefaultMouseSensitivity = 0.35f;

		constexpr float kPi = std::numbers::pi_v<float>;
		constexpr float kHardPitchLimit = 89.f * kDegToRad;

		RE::NiPoint3 DirectionOf(float a_yaw, float a_pitch)
		{
			const float cp = std::cos(a_pitch);
			return { std::sin(a_yaw) * cp, std::cos(a_yaw) * cp, -std::sin(a_pitch) };
		}

		// Shortest signed distance from a_angle to zero, so recentering never takes the
		// long way round after the player spun the camera past half a turn.
		float WrapAngle(float a_angle)
		{
			constexpr float twoPi = 2.f * std::numbers::pi_v<float>;
			a_angle = std::fmod(a_angle + std::numbers::pi_v<float>, twoPi);
			if (a_angle < 0.f) {
				a_angle += twoPi;
			}
			return a_angle - std::numbers::pi_v<float>;
		}

		float DecayToZero(float a_value, float a_step)
		{
			if (a_value > a_step) {
				return a_value - a_step;
			}
			if (a_value < -a_step) {
				return a_value + a_step;
			}
			return 0.f;
		}

		// The camera's own "up" for a heading: DirectionOf with the pitch taken a quarter
		// turn back. Level heading gives world up; in a dive it tilts with the view, so the
		// height offset stays "above the arrow" on screen instead of above it in the world.
		RE::NiPoint3 UpOf(float a_yaw, float a_pitch)
		{
			const float sp = std::sin(a_pitch);
			return { std::sin(a_yaw) * sp, std::cos(a_yaw) * sp, std::cos(a_pitch) };
		}

		// Critically damped spring toward a target (the usual game "SmoothDamp"). Reaches
		// the target in roughly a_smoothTime without overshoot, and carries its velocity
		// across frames so a moving target is followed smoothly rather than chased in
		// steps. a_smoothTime 0 snaps.
		float SmoothDamp(float a_current, float a_target, float& a_velocity, float a_smoothTime, float a_delta)
		{
			if (a_smoothTime <= 0.f) {
				a_velocity = 0.f;
				return a_target;
			}
			const float omega = 2.f / a_smoothTime;
			const float x = omega * a_delta;
			const float decay = 1.f / (1.f + x + 0.48f * x * x + 0.235f * x * x * x);
			const float change = a_current - a_target;
			const float temp = (a_velocity + omega * change) * a_delta;
			a_velocity = (a_velocity - omega * temp) * decay;
			return a_target + (change + temp) * decay;
		}

		float SmoothDampAngle(float a_current, float a_target, float& a_velocity, float a_smoothTime, float a_delta)
		{
			// Target moved next to the current angle, so crossing +-180 is not a full spin.
			return SmoothDamp(a_current, a_current + WrapAngle(a_target - a_current), a_velocity, a_smoothTime,
				a_delta);
		}

		// NiQuaternion::FromRotation (SE REL::ID 69467), line for line, as reverse-engineered
		// from the game. CommonLib has no matrix -> quaternion.
		RE::NiQuaternion QuaternionFromRotation(const RE::NiMatrix3& a_m)
		{
			const auto&      m = a_m.entry;
			RE::NiQuaternion q{};

			const float trace = m[0][0] + m[1][1] + m[2][2];
			if (trace > 0.f) {
				const float s = std::sqrt(trace + 1.f);
				const float r = 0.5f / s;
				q.w = s * 0.5f;
				q.x = (m[2][1] - m[1][2]) * r;
				q.y = (m[0][2] - m[2][0]) * r;
				q.z = (m[1][0] - m[0][1]) * r;
				return q;
			}

			constexpr std::size_t next[3]{ 1, 2, 0 };
			std::size_t           i = m[1][1] > m[0][0] ? 1 : 0;
			if (m[2][2] > m[i][i]) {
				i = 2;
			}
			const std::size_t j = next[i];
			const std::size_t k = next[j];

			const float s = std::sqrt(m[i][i] - m[j][j] - m[k][k] + 1.f);
			const float r = 0.5f / s;
			float*      v[3]{ &q.x, &q.y, &q.z };
			*v[i] = s * 0.5f;
			q.w = (m[k][j] - m[j][k]) * r;
			*v[j] = (m[i][j] + m[j][i]) * r;
			*v[k] = (m[i][k] + m[k][i]) * r;
			return q;
		}

		// What FreeCameraState::GetRotation computes, plus the roll it hardcodes to zero.
		// The engine calls FromEulerAnglesZXY(M, yaw, pitch, roll) = Rz * Rx * Ry; CommonLib's
		// EulerAnglesToAxesZXY(x, y, z) expands to the same matrix entry for entry.
		RE::NiQuaternion CameraRotation(float a_yaw, float a_pitch, float a_roll)
		{
			RE::NiMatrix3 m;
			m.EulerAnglesToAxesZXY(a_pitch, a_roll, a_yaw);
			return QuaternionFromRotation(m);
		}

		// ---- cost ----

		RE::ActorValue PoolValue(Settings::CostPool a_pool)
		{
			switch (a_pool) {
			case Settings::CostPool::kStamina:
				return RE::ActorValue::kStamina;
			case Settings::CostPool::kMagicka:
				return RE::ActorValue::kMagicka;
			case Settings::CostPool::kHealth:
				return RE::ActorValue::kHealth;
			default:
				return RE::ActorValue::kNone;
			}
		}

		std::string_view ShortageToken(Settings::CostPool a_pool)
		{
			switch (a_pool) {
			case Settings::CostPool::kMagicka:
				return "$STB_PC_NoMagicka"sv;
			case Settings::CostPool::kHealth:
				return "$STB_PC_NoHealth"sv;
			default:
				return "$STB_PC_NoStamina"sv;
			}
		}

		// What the pool may not go below. Health never reaches zero: a cost that kills the
		// player mid-shot is not a cost, it is a bug.
		float PoolFloor(Settings::CostPool a_pool)
		{
			return a_pool == Settings::CostPool::kHealth ? 1.f : 0.f;
		}

		float Available(RE::PlayerCharacter* a_player, Settings::CostPool a_pool)
		{
			const float current = a_player->AsActorValueOwner()->GetActorValue(PoolValue(a_pool));
			return (std::max)(current - PoolFloor(a_pool), 0.f);
		}

		// As a damage modifier, the way the game spends stamina on a power attack: it
		// regenerates normally afterwards.
		void Spend(RE::PlayerCharacter* a_player, Settings::CostPool a_pool, float a_amount)
		{
			if (a_amount > 0.f) {
				a_player->AsActorValueOwner()->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage,
					PoolValue(a_pool), -a_amount);
			}
		}

		// Any of the four things behind a shot may be the one listed: the projectile form
		// itself, the spell (hands, staves and scrolls all set it), the ammo or the weapon.
		// So a player can name "Firebolt", "Daedric Arrow" or a particular bow and get what
		// they meant, without knowing which of those Skyrim treats as the projectile.
		bool IsAlwaysRide(RE::Projectile* a_projectile, const RE::Projectile::PROJECTILE_RUNTIME_DATA& a_runtime)
		{
			if (!Settings::HasAlwaysList()) {
				return false;
			}
			const auto listed = [](const RE::TESForm* a_form) {
				return a_form && Settings::AlwaysRides(a_form->GetFormID());
			};
			return listed(a_projectile->GetProjectileBase()) || listed(a_runtime.spell) ||
			       listed(a_runtime.ammoSource) || listed(a_runtime.weaponSource);
		}

		const Settings::Cost& CostRule(Session::Kind a_kind)
		{
			return a_kind == Session::Kind::kMissile ? Settings::SpellCost() : Settings::ArrowCost();
		}

		RE::BGSPerk* RequiredPerk(Session::Kind a_kind)
		{
			return a_kind == Session::Kind::kMissile ? Settings::SpellPerk() : Settings::ArrowPerk();
		}

		void NotifyShortage(Settings::CostPool a_pool)
		{
			const std::string text = Localization::Get(ShortageToken(a_pool));
			RE::DebugNotification(text.c_str());
		}

		// Where the arrow is drawn. The model's node is what ends up on screen, so framing
		// it rather than the reference keeps the two in step when the camera and the
		// projectile update in different orders within a frame.
		RE::NiPoint3 ArrowAnchor(RE::Projectile* a_projectile)
		{
			if (auto* node = a_projectile->Get3D()) {
				return node->world.translate;
			}
			return a_projectile->GetPosition();
		}
	}

	namespace
	{
		// The loaded grid cell under a point, or nullptr where nothing is loaded. By
		// position on purpose: a flying projectile keeps the cell it was fired in as its
		// parent (the log showed it never changing), so its own parent cell says nothing
		// about where it is now.
		RE::TESObjectCELL* LoadedCellAt(const RE::NiPoint3& a_pos)
		{
			auto* tes = RE::TES::GetSingleton();
			return tes ? tes->GetCell(a_pos) : nullptr;
		}
	}

	float Session::RealDelta(Clock::time_point& a_last)
	{
		const auto now = Clock::now();
		if (a_last == Clock::time_point{}) {
			a_last = now;
			return 0.f;
		}
		const float delta = std::chrono::duration<float>(now - a_last).count();
		a_last = now;
		// A long frame (loading screen, alt-tab) must not snap the camera or the arrow.
		return std::clamp(delta, 0.f, 0.1f);
	}

	RE::Projectile* Session::GetProjectile()
	{
		if (!_active || !_arrow) {
			return nullptr;
		}
		const auto ptr = _handle.get();
		if (!ptr || ptr.get() != static_cast<RE::TESObjectREFR*>(_arrow)) {
			return nullptr;
		}
		return _arrow;
	}

	void Session::TryCapture(RE::Projectile* a_projectile, Kind a_kind)
	{
		if (_active || !a_projectile) {
			return;
		}
		// The common case -- no key held, no always-ride list -- leaves before touching the
		// projectile: this runs for every arrow and spell bolt in the loaded area.
		const bool keyHeld = Input::TriggerHeld();
		if (!keyHeld && !Settings::HasAlwaysList()) {
			return;
		}

		auto& runtime = a_projectile->GetProjectileRuntimeData();
		if (runtime.livingTime > kFreshArrowSeconds) {
			return;
		}

		// A listed form needs no key and ignores the per-kind switches: naming a spell or an
		// arrow in sAlwaysRide is the player asking for exactly that one.
		const bool listed = IsAlwaysRide(a_projectile, runtime);
		if (!keyHeld && !listed) {
			return;
		}
		if (!listed) {
			if (a_kind == Kind::kMissile) {
				if (!Settings::Spells()) {
					return;
				}
			} else {
				const bool bolt = runtime.weaponSource && runtime.weaponSource->IsCrossbow();
				if (bolt ? !Settings::Bolts() : !Settings::Arrows()) {
					return;
				}
			}
		}
		if (runtime.flags & static_cast<std::uint32_t>(RE::Projectile::Flags::kDestroyed)) {
			return;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || runtime.shooter != player->GetHandle()) {
			return;
		}
		if (!a_projectile->Get3D()) {
			return;
		}
		if (!MayStart(player, a_projectile, a_kind)) {
			return;
		}

		// Paid only once the ride is really up: if the engine refuses the free camera the
		// shot costs nothing.
		if (Start(a_projectile, a_kind)) {
			if (const auto& cost = CostRule(a_kind); cost.pool != Settings::CostPool::kNone) {
				Spend(player, cost.pool, (std::min)(cost.start, Available(player, cost.pool)));
			}
		}
	}

	bool Session::MayStart(RE::PlayerCharacter* a_player, RE::Projectile* a_projectile, Kind a_kind)
	{
		// No message for a missing perk: without it the trigger simply is not a thing the
		// character can do yet, like any other perk-gated power.
		if (auto* perk = RequiredPerk(a_kind); perk && !a_player->HasPerk(perk)) {
			return false;
		}

		const auto& cost = CostRule(a_kind);
		const auto  pool = cost.pool;
		if (pool == Settings::CostPool::kNone || !cost.cancelOnShortage) {
			return true;
		}
		const float needed = cost.start;
		// With only an upkeep configured, there has to be something left to pay it from.
		const bool short_ = needed > 0.f ? Available(a_player, pool) < needed :
		                                   cost.perSecond > 0.f && Available(a_player, pool) <= 0.f;
		if (!short_) {
			return true;
		}
		if (_rejectedArrow != a_projectile) {
			_rejectedArrow = a_projectile;
			NotifyShortage(pool);
			logger::debug("ride refused: not enough in the pool");
		}
		return false;
	}

	bool Session::PayUpkeep(float a_delta)
	{
		const auto& rule = CostRule(_kind);
		const auto  pool = rule.pool;
		const float rate = rule.perSecond;
		if (pool == Settings::CostPool::kNone || rate <= 0.f || a_delta <= 0.f) {
			return true;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return true;
		}

		const float due = rate * a_delta;
		const float left = Available(player, pool);
		Spend(player, pool, (std::min)(due, left));

		// Off: the pool is drained to its floor and the ride carries on regardless.
		if (rule.cancelOnShortage && left <= due) {
			NotifyShortage(pool);
			return false;
		}
		return true;
	}

	bool Session::Start(RE::Projectile* a_projectile, Kind a_kind)
	{
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!camera || camera->IsInFreeCameraMode()) {
			return false;
		}

		auto* ui = RE::UI::GetSingleton();
		if (!ui || ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME)) {
			return false;
		}

		auto& runtime = a_projectile->GetProjectileRuntimeData();

		_arrow = a_projectile;
		_kind = a_kind;
		_handle = a_projectile->GetHandle();
		_originalRange = -1.f;

		const auto velocity = runtime.linearVelocity;
		if (const float speed = velocity.Length(); speed > 1.f) {
			_dirYaw = std::atan2(velocity.x, velocity.y);
			_dirPitch = -std::asin(std::clamp(velocity.z / speed, -1.f, 1.f));
		} else {
			_dirYaw = 0.f;
			_dirPitch = 0.f;
		}
		_yawRate = 0.f;
		_pitchRate = 0.f;
		_mouseYawRate = 0.f;
		_followYaw = _dirYaw;
		_followPitch = _dirPitch;
		_followYawVel = 0.f;
		_followPitchVel = 0.f;
		_roll = 0.f;
		_rollVel = 0.f;
		_orbitYaw = 0.f;
		_orbitPitch = 0.f;
		_cameraYaw = _dirYaw;
		_cameraPitch = _dirPitch;
		_lastAnchor = ArrowAnchor(a_projectile);
		_cameraPos = _lastAnchor - DirectionOf(_cameraYaw, _cameraPitch) * Settings::Distance() +
		             UpOf(_cameraYaw, _cameraPitch) * Settings::Height();
		_rotationChecked = false;
		_lingering = false;
		_lastMouseMove = Clock::time_point{};

		const auto now = Clock::now();
		_startedAt = now;
		_lastSeen = now;
		_lastSteer = Clock::time_point{};
		_lastCamera = Clock::time_point{};

		// Free camera first: if the engine refuses it there is nothing to restore yet.
		camera->ToggleFreeCameraMode(false);
		if (!camera->IsInFreeCameraMode()) {
			_arrow = nullptr;
			_handle = {};
			logger::warn("free camera refused, no ride");
			return false;
		}

		_active = true;
		_shotOrigin = _lastAnchor;

		// The projectile itself is only touched once the ride is really up: with the free
		// camera refused, the shot must fly exactly as the game fired it.
		if (const float scale = Settings::SpeedScale(); scale != 1.f && scale > 0.f) {
			runtime.linearVelocity *= scale;
		}
		// Life: MissileProjectile::UpdateImpl (arrows run it too) kills the projectile once
		// distanceMoved passes range (checked in the game code). Stretching range is
		// stretching its life.
		if (const float life = Settings::LifeScale(); life != 1.f) {
			_originalRange = runtime.range;
			runtime.range = life <= 0.f ? (std::numeric_limits<float>::max)() :
			                              (std::min)(runtime.range * life, (std::numeric_limits<float>::max)());
		}
		_hintVisible.store(true, std::memory_order_relaxed);
		BlockControls();
		Input::BeginSession();
		TimeScale::Apply(Settings::WorldScale(), Settings::PlayerScale());

		logger::debug("ride started on arrow {:08X}", a_projectile->GetFormID());
		return true;
	}

	void Session::RequestStop(std::string_view a_reason)
	{
		if (!_active || _stopPending) {
			return;
		}
		_stopPending = true;
		_stopReason = a_reason;
	}

	void Session::QueueFlush()
	{
		if (_flushQueued) {
			return;
		}
		_flushQueued = true;
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([]() { Session::FlushStop(); });
		} else {
			FlushStop();
		}
	}

	void Session::FlushStop()
	{
		_flushQueued = false;
		if (_stopPending) {
			Stop(_stopReason);
		}
	}

	void Session::Stop(std::string_view a_reason)
	{
		_stopPending = false;
		if (!_active) {
			return;
		}
		// Before _active drops: GetProjectile only answers during a ride.
		RestoreRange();
		_active = false;
		_lingering = false;
		_hintVisible.store(false, std::memory_order_relaxed);

		TimeScale::Clear();
		Input::EndSession();
		RestoreControls();
		SaveTimeIfChanged();

		// ToggleFreeCameraMode pops the state that was pushed on the way in, so the player
		// lands back in whatever camera they were using. Logged either way: a camera still
		// reporting free mode here is the player stuck in it, and the log line is the only
		// way to tell that apart from the ride simply not ending.
		if (auto* camera = RE::PlayerCamera::GetSingleton(); camera) {
			if (camera->IsInFreeCameraMode()) {
				camera->ToggleFreeCameraMode(false);
				if (camera->IsInFreeCameraMode()) {
					logger::error("free camera did not pop, forcing third person");
					camera->ForceThirdPerson();
				}
			} else {
				logger::debug("camera had already left free mode");
			}
		}

		_arrow = nullptr;
		_handle = {};

		logger::info("ride ended: {}", a_reason);
	}

	void Session::Steer(RE::Projectile* a_projectile)
	{
		_lastSeen = Clock::now();
		if (_lingering) {
			// The flight is over; whatever the engine does with the arrow now (stick,
			// bounce, fall) is left alone.
			return;
		}

		auto&       runtime = a_projectile->GetProjectileRuntimeData();
		const auto  velocity = runtime.linearVelocity;
		const float speed = velocity.Length();
		if (speed < kStoppedSpeed) {
			BeginLinger("arrow came to rest"sv);
			return;
		}

		// Steering runs on real time, not the delta the engine hands the projectile: that
		// one is already multiplied by the slow motion, so a 0.15 scale would make the
		// arrow answer the keys six times slower the moment the ride starts.
		const float delta = RealDelta(_lastSteer);
		if (delta <= 0.f) {
			return;
		}

		// Charged on real time too, for the same reason as the steering: per game second it
		// would be nearly free under the slow motion.
		if (!PayUpkeep(delta)) {
			// Not the linger: the arrow has not hit anything, the player just cannot hold
			// it any longer. It flies on by itself.
			RequestStop("pool ran dry"sv);
			return;
		}

		ReadFlight(velocity / speed);

		const float maxRate = Settings::TurnRate() * kDegToRad;
		// W is nose up: the player is looking over the arrow, so forward reads as "up the
		// screen". bInvertY in the INI flips it for anyone who expects a flight stick.
		const float pitchInput = Settings::InvertSteerY() ? Input::SteerY() : -Input::SteerY();

		// The turn rate eases toward what the keys ask for instead of jumping to it. A key
		// is 0 or 1; applied directly it puts a kink in the path at every press and
		// release, which is most of what made the ride feel jerky. Eased, the same key
		// flies a clean arc -- the way a plane rolls into and out of a turn.
		const float response = Settings::TurnResponse();
		const float blend = response > 0.f ? 1.f - std::exp(-response * delta) : 1.f;
		_yawRate += (Input::SteerX() * maxRate - _yawRate) * blend;
		_pitchRate += (pitchInput * maxRate - _pitchRate) * blend;

		float yawStep = _yawRate * delta;
		float pitchStep = _pitchRate * delta;

		// The mouse, while its key is held: a direct turn, as far as the hand moves, on
		// top of the keys. Mouse down is nose down, like looking down; bInvertMouseY flips
		// it here too.
		const float mouseBlend = 1.f - std::exp(-kMouseBankResponse * delta);
		if (Settings::MouseSteerKey() != 0 && Input::MouseSteerHeld()) {
			float mouseX = 0.f;
			float mouseY = 0.f;
			Input::ConsumeMouse(mouseX, mouseY);
			const float sensitivity = Settings::MouseSteerSensitivity() * kDegToRad;
			const float mouseYaw = mouseX * sensitivity;
			yawStep += mouseYaw;
			pitchStep += (Settings::InvertMouseY() ? -mouseY : mouseY) * sensitivity;
			_mouseYawRate += (mouseYaw / delta - _mouseYawRate) * mouseBlend;
		} else {
			_mouseYawRate -= _mouseYawRate * mouseBlend;
		}

		// Upside down, in the top half of a loop, the heading points behind the arrow, so
		// a turn to the right of the screen is a turn to the left of the heading.
		const float flip = std::cos(_dirPitch) < 0.f ? -1.f : 1.f;
		_dirYaw = WrapAngle(_dirYaw + yawStep * flip);
		_dirPitch += pitchStep;

		if (Settings::AllowLoops()) {
			_dirPitch = WrapAngle(_dirPitch);
		} else {
			// At the limit the rate pushing into it is dropped too, or it would keep
			// building and the arrow would stick there for a moment after the key is let go.
			const float limit = Settings::PitchLimit() * kDegToRad;
			if (_dirPitch > limit) {
				_dirPitch = limit;
				_pitchRate = (std::min)(_pitchRate, 0.f);
			} else if (_dirPitch < -limit) {
				_dirPitch = -limit;
				_pitchRate = (std::max)(_pitchRate, 0.f);
			}
		}

		const RE::NiPoint3 dir = DirectionOf(_dirYaw, _dirPitch);
		runtime.linearVelocity = dir * speed;

		// The model has to be turned by hand. Projectile::Move_0 only moves the Havok
		// phantom; the model is drawn from the reference's own angles, and after launch the
		// only thing that ever touches them is ApplyGravity -- pitch only, and not at all
		// while gravity is scaled to zero. Without this the arrow flies wherever it is
		// steered while still pointing where it was shot: turned round, it goes tail first.
		// Same fields TESObjectREFR::SetAngleOnReferenceX/Z write (angle at +0x48), minus
		// the save-game change tracking a transient projectile does not need.
		//
		// The reference's angles only reach +-90 in pitch, so they are taken from the
		// direction itself; in the top of a loop that is the same line through space, and
		// straight up or down the heading from before stands in for the undefined yaw.
		const float horizontal = std::sqrt(dir.x * dir.x + dir.y * dir.y);
		a_projectile->data.angle.x = -std::asin(std::clamp(dir.z, -1.f, 1.f));
		a_projectile->data.angle.z = horizontal > 1e-4f ? std::atan2(dir.x, dir.y) :
		                             flip > 0.f         ? _dirYaw :
		                                                  WrapAngle(_dirYaw + kPi);

		// The other velocity field (+0xF0) is a unit direction: Sync3DWithReference clamps
		// its z to [-1, 1] and takes asin of it to re-pitch the model when the projectile
		// has kGravityUpdateModel. Left stale, it would pull the nose back to the launch
		// pitch. Written only when it really is unit length, so a projectile that uses the
		// field for something else is left alone.
		if (const float len = runtime.velocity.Length(); len > 0.99f && len < 1.01f) {
			runtime.velocity = dir;
		}
	}

	void Session::ReadFlight(const RE::NiPoint3& a_dir)
	{
		// What the arrow is really doing now -- gravity, another mod pulling on it -- read
		// back into heading and pitch before the input is added. Pitch is measured in the
		// vertical plane of the heading: "along" is signed, so a direction behind the
		// heading reads as pitch beyond +-90 instead of the heading flipping round, which
		// is what lets a loop carry on over the top rather than bounce off the vertical.
		const float horizontal = std::sqrt(a_dir.x * a_dir.x + a_dir.y * a_dir.y);
		float       along = a_dir.x * std::sin(_dirYaw) + a_dir.y * std::cos(_dirYaw);
		// Near the vertical the horizontal part is tiny and its direction noise; the
		// heading from before is kept.
		if (horizontal > 0.2f) {
			const float yaw = std::atan2(a_dir.x, a_dir.y);
			_dirYaw = along >= 0.f ? yaw : WrapAngle(yaw + kPi);
			along = along >= 0.f ? horizontal : -horizontal;
		}
		_dirPitch = std::atan2(-a_dir.z, along);

		// Without loops the pitch never leaves +-90: fold a backwards reading round.
		if (!Settings::AllowLoops() && std::abs(_dirPitch) > kPi / 2.f) {
			_dirYaw = WrapAngle(_dirYaw + kPi);
			_dirPitch = (_dirPitch > 0.f ? kPi : -kPi) - _dirPitch;
		}
	}

	void Session::RestoreRange()
	{
		if (_originalRange < 0.f) {
			return;
		}
		// Still the same live projectile? Then it gets its own range back; past it, the
		// engine removes it on the next update, as it would have without the ride.
		if (auto* projectile = GetProjectile()) {
			projectile->GetProjectileRuntimeData().range = _originalRange;
		}
		_originalRange = -1.f;
	}

	void Session::AdjustTime(int a_steps)
	{
		const float world = Settings::WorldScale();
		const float player = Settings::PlayerScale();
		// World and player move together when they were set the same (the default); a
		// player scale set apart on purpose is left where it is.
		const bool  linked = std::abs(world - player) < 1e-4f;
		const float factor = std::pow(kTimeStep, static_cast<float>(a_steps));
		// Three decimals, so the INI gets 0.188 rather than 0.18750001.
		const float next = std::round(std::clamp(world * factor, 0.01f, 1.f) * 1000.f) / 1000.f;
		Settings::SetTimeScale(next, linked ? next : player);
		_timeChanged = true;
		logger::debug("slow motion set to {:.3f} by key", next);
	}

	void Session::SaveTimeIfChanged()
	{
		if (_timeChanged) {
			_timeChanged = false;
			Settings::Save();
		}
	}

	float Session::GravityScale()
	{
		// Only while it is being flown: an arrow that bounced off a wall during the linger
		// has to fall like any other, not hang in the air.
		return _lingering ? 1.f : Settings::GravityScale();
	}

	void Session::UpdateCamera()
	{
		auto* projectile = GetProjectile();
		if (!projectile && !_lingering) {
			return;
		}

		const auto  now = Clock::now();
		const float delta = RealDelta(_lastCamera);

		if (_lingering && Settings::LingerEaseTime()) {
			// Slow at the moment of the hit, normal speed by the time the camera hands
			// back, so the target's reaction plays out instead of crawling.
			const float linger = Settings::ImpactLinger();
			const float t = linger > 0.f ?
			                    std::clamp(std::chrono::duration<float>(now - _lingerStart).count() / linger, 0.f, 1.f) :
			                    1.f;
			const float eased = t * t * (3.f - 2.f * t);
			TimeScale::Apply(Settings::WorldScale() + (1.f - Settings::WorldScale()) * eased,
				Settings::PlayerScale() + (1.f - Settings::PlayerScale()) * eased);
		} else if (const int steps = Input::ConsumeTimeSteps(); steps != 0) {
			AdjustTime(steps);
			TimeScale::Apply(Settings::WorldScale(), Settings::PlayerScale());
		} else {
			TimeScale::Reapply();
		}

		if (projectile) {
			const auto& runtime = projectile->GetProjectileRuntimeData();
			// The heading itself is kept by Steer, loop and all. After the hit it is
			// frozen: the velocity is whatever the collision left -- a bounce, a stop, a
			// spin -- and following it would whip the camera around at the worst moment.
			if (!(runtime.flags & static_cast<std::uint32_t>(RE::Projectile::Flags::kDestroyed))) {
				_lastAnchor = ArrowAnchor(projectile);
			}
		}

		// While the mouse steers, Steer takes the mouse and the view swings back behind the
		// projectile, so what the hand does is what the screen shows.
		const bool mouseSteering = !_lingering && Settings::MouseSteerKey() != 0 && Input::MouseSteerHeld();
		if (mouseSteering) {
			const float keep = std::exp(-kSteerRecenter * delta);
			_orbitYaw *= keep;
			_orbitPitch *= keep;
		} else if (!Settings::MouseLook()) {
			// Still drained: movement piling up here would jump the arrow the moment the
			// steer key goes down.
			float ignoredX = 0.f;
			float ignoredY = 0.f;
			Input::ConsumeMouse(ignoredX, ignoredY);
		} else {
			float mouseX = 0.f;
			float mouseY = 0.f;
			Input::ConsumeMouse(mouseX, mouseY);
			if (mouseX != 0.f || mouseY != 0.f) {
				_lastMouseMove = now;
			}

			const float sensitivity = Settings::MouseSensitivity() * kDegToRad;
			_orbitYaw += mouseX * sensitivity;
			_orbitPitch += (Settings::InvertMouseY() ? -mouseY : mouseY) * sensitivity;

			// The right stick does the same as a rate: held over, it keeps turning. Scaled
			// by the same sensitivity, relative to its default. Stick up looks up.
			const float lookX = Input::LookX();
			const float lookY = Input::LookY();
			if (lookX != 0.f || lookY != 0.f) {
				_lastMouseMove = now;
				const float rate = kStickLookRate * kDegToRad * (Settings::MouseSensitivity() / kDefaultMouseSensitivity) * delta;
				_orbitYaw += lookX * rate;
				_orbitPitch += (Settings::InvertMouseY() ? lookY : -lookY) * rate;
			}

			const float orbitLimit = Settings::MousePitchLimit() * kDegToRad;
			_orbitYaw = WrapAngle(_orbitYaw);
			_orbitPitch = std::clamp(_orbitPitch, -orbitLimit, orbitLimit);

			// Recentering is off by default: a view that drifts back while the hand is
			// still on the mouse made mouse look pointless. When enabled it waits until the
			// mouse has rested for fRecenterDelay.
			const float recenter = Settings::RecenterSpeed();
			const bool  rested = _lastMouseMove == Clock::time_point{} ||
			                    std::chrono::duration<float>(now - _lastMouseMove).count() >= Settings::RecenterDelay();
			if (recenter > 0.f && delta > 0.f && rested) {
				const float step = recenter * kDegToRad * delta;
				_orbitYaw = DecayToZero(_orbitYaw, step);
				_orbitPitch = DecayToZero(_orbitPitch, step);
			}
		}

		// Chase camera, the way flight games do it. The HEADING is what gets smoothed: it
		// trails the arrow's through a critically damped spring, so a turn first shows the
		// arrow swinging its nose across the screen and then the view swinging after it.
		// The POSITION is not smoothed at all -- it is bolted to that trailing heading at a
		// fixed distance. The previous version did the opposite (instant heading, position
		// lerped in world space) and that is what felt loose: the lerp cut across the
		// orbit on every turn, the distance breathed with the arrow's speed, and the arrow
		// slid around the frame.
		const float lag = Settings::FollowLag();
		_followYaw = WrapAngle(SmoothDampAngle(_followYaw, _dirYaw, _followYawVel, lag, delta));
		_followPitch = WrapAngle(SmoothDampAngle(_followPitch, _dirPitch, _followPitchVel, lag, delta));

		// Bank into the turn in proportion to how hard the arrow is turning. Driven by the
		// eased turn rate, so it rolls in and out as smoothly as the arrow does. Negative:
		// a positive Y rotation raises the camera's right side, which is a bank to the
		// left, and a right turn (yaw increasing) should lean right.
		const float maxRate = Settings::TurnRate() * kDegToRad;
		const float bankTarget = maxRate > 0.f ? -std::clamp((_yawRate + _mouseYawRate) / maxRate, -1.f, 1.f) *
		                                             Settings::BankAngle() * kDegToRad :
		                                         0.f;
		_roll = SmoothDamp(_roll, bankTarget, _rollVel, Settings::BankLag(), delta);

		_cameraYaw = WrapAngle(_followYaw + _orbitYaw);
		// With loops the camera goes round with the arrow, upside down over the top: the
		// rotation is built from the angles directly, so pitch past 90 degrees is just more
		// of the same turn. Without them it stays short of the vertical.
		_cameraPitch = Settings::AllowLoops() ?
		                   WrapAngle(_followPitch + _orbitPitch) :
		                   std::clamp(_followPitch + _orbitPitch, -kHardPitchLimit, kHardPitchLimit);

		_cameraPos = _lastAnchor - DirectionOf(_cameraYaw, _cameraPitch) * Settings::Distance() +
		             UpOf(_cameraYaw, _cameraPitch) * Settings::Height();
	}

	bool Session::PrepareCamera(RE::FreeCameraState* a_state)
	{
		// This runs on every frame of a ride by construction -- it is what makes the
		// camera follow -- so the safety checks live here too and no longer depend on
		// PlayerCharacter::Update firing. The teardown itself is deferred to a task: it
		// toggles the camera state, and doing that from inside FreeCameraState::Update
		// would re-enter the state machine that is currently running.
		CheckAlive();
		if (_stopPending) {
			QueueFlush();
			return false;
		}
		if (!_active) {
			return false;
		}

		UpdateCamera();
		// Kept in the state too (rotation.x = pitch, rotation.y = yaw), so the free camera
		// stays coherent with what is on screen; the quaternion itself is replaced in
		// WriteRotation because the original cannot carry a roll.
		// The state only knows pitches short of the vertical; the quaternion carries the rest.
		a_state->rotation.x = std::clamp(_cameraPitch, -kHardPitchLimit, kHardPitchLimit);
		a_state->rotation.y = _cameraYaw;
		return true;
	}

	void Session::WriteRotation(RE::NiQuaternion& a_rotation)
	{
		// One-time proof that the hand-built quaternion is the engine's: with the roll left
		// out it has to match what the original GetRotation just produced from the same
		// yaw and pitch. q and -q are the same rotation, hence the absolute dot product.
		if (!_rotationChecked && Settings::DebugLog()) {
			_rotationChecked = true;
			const auto  mine = CameraRotation(_cameraYaw, _cameraPitch, 0.f);
			const float dot = mine.w * a_rotation.w + mine.x * a_rotation.x + mine.y * a_rotation.y +
			                  mine.z * a_rotation.z;
			logger::debug("camera quaternion self-test: |dot| = {:.6f} (1.0 = identical to the engine's)",
				std::abs(dot));
		}

		a_rotation = CameraRotation(_cameraYaw, _cameraPitch, _roll);
	}

	void Session::ApplyCameraTranslation(RE::FreeCameraState* a_state)
	{
		a_state->translation = _cameraPos;
	}

	void Session::CheckAlive()
	{
		if (!_active || _stopPending) {
			return;
		}
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!camera || !camera->IsInFreeCameraMode()) {
			// Something else owns the camera now. Tear down in place rather than through
			// RequestStop: Stop must not toggle a free camera that is no longer ours.
			RestoreRange();
			_active = false;
			_lingering = false;
			_hintVisible.store(false, std::memory_order_relaxed);
			TimeScale::Clear();
			Input::EndSession();
			RestoreControls();
			SaveTimeIfChanged();
			_arrow = nullptr;
			_handle = {};
			logger::info("ride ended: camera taken over");
			return;
		}

		// Hard stops: these end the ride at once, linger or not.
		if (Input::ConsumeCancel()) {
			// Straight out, no linger: the player asked for the camera back. The arrow
			// keeps flying on its own -- with normal gravity, since it is no longer tracked.
			RequestStop("cancelled"sv);
			return;
		}
		const auto now = Clock::now();
		if (std::chrono::duration<float>(now - _startedAt).count() > Settings::MaxDuration()) {
			RequestStop("safety timeout"sv);
			return;
		}
		if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
			RequestStop("main menu"sv);
			return;
		}

		if (_lingering) {
			if (std::chrono::duration<float>(now - _lingerStart).count() >= Settings::ImpactLinger()) {
				RequestStop(_lingerReason);
			}
			return;
		}

		// End of flight: every one of these leads into the linger. None of them may be
		// skipped just because another fired first -- a killed arrow never reports the
		// impact flag, a stuck one never reports being gone.
		auto* projectile = GetProjectile();
		if (!projectile) {
			BeginLinger("arrow gone"sv);
			return;
		}

		const std::uint32_t flags = projectile->GetProjectileRuntimeData().flags;
		if (flags & static_cast<std::uint32_t>(RE::Projectile::Flags::kDestroyed)) {
			BeginLinger("arrow destroyed"sv);
			return;
		}
		// Set by Projectile::ProcessImpacts once it has walked the impact list, which is
		// the engine's own "this projectile has hit something". An arrow that sticks into
		// a wall is never destroyed, so this is the signal that actually fires on a hit.
		if (flags & static_cast<std::uint32_t>(RE::Projectile::Flags::kProcessedImpacts)) {
			BeginLinger("impact"sv);
			return;
		}
		if (std::chrono::duration<float>(now - _lastSeen).count() > kLostArrowSeconds) {
			BeginLinger("arrow stopped updating"sv);
			return;
		}

		// Range: the cap, and the edge of the loaded world. Past that edge there is no
		// collision and the engine deletes the shot, so the camera hands back instead of
		// following it into LOD. Not a linger -- there is nothing to watch.
		if (const float cap = Settings::MaxDistance(); cap > 0.f && _lastAnchor.GetDistance(_shotOrigin) > cap) {
			RequestStop("range cap"sv);
			return;
		}
		// Outdoors the test is the grid under the shot's position; indoors the whole cell
		// is loaded and only a detached cell counts. No cell at all is left to the other
		// checks: a fresh projectile can be between cells for a frame.
		auto* cell = projectile->GetParentCell();
		if (cell && (cell->IsExteriorCell() ? LoadedCellAt(_lastAnchor) == nullptr : !cell->IsAttached())) {
			RequestStop("left the loaded area"sv);
		}
	}

	void Session::BeginLinger(std::string_view a_reason)
	{
		if (!_active || _lingering || _stopPending) {
			return;
		}
		if (Settings::ImpactLinger() <= 0.f) {
			RequestStop(a_reason);
			return;
		}

		_lingering = true;
		_hintVisible.store(false, std::memory_order_relaxed);
		_lingerStart = Clock::now();
		_lingerReason = a_reason;

		// The arrow is no longer being flown: drop the turn so the bank rolls back to
		// level, and stop listening to the steering keys.
		_yawRate = 0.f;
		_pitchRate = 0.f;
		_mouseYawRate = 0.f;

		logger::debug("flight over ({}), holding the camera for {:.2f}s", a_reason, Settings::ImpactLinger());
	}

	void Session::Watchdog()
	{
		CheckAlive();
		FlushStop();
	}

	void Session::BlockControls()
	{
		auto* controls = RE::ControlMap::GetSingleton();
		if (!controls) {
			_savedControls = 0;
			return;
		}
		_savedControls = static_cast<std::uint32_t>(controls->enabledControls.get());
		// Not ToggleControls. That broadcasts UserEventEnabled, and PlayerCharacter's sink
		// for it (SE REL::ID 39652) answers "fighting off, weapon drawn" with
		// PlayerControls::DoAction(kActionSheath) -- the player lowered the bow the moment
		// the ride began -- and "sneaking off" by standing the player up out of sneak.
		// The only other sink (HUDMenu) reacts to the movement bit alone. The handlers
		// that actually stop an attack read the mask itself, so writing it without the
		// event blocks the shot and skips both side effects.
		WriteControlsSilently(controls, kBlockedControls, false);
	}

	void Session::RestoreControls()
	{
		auto* controls = RE::ControlMap::GetSingleton();
		if (!controls || _savedControls == 0) {
			_savedControls = 0;
			return;
		}
		// Only hand back what was on before the ride: something else may have switched a
		// group off for its own reasons while the arrow was in the air.
		if (const std::uint32_t restore = _savedControls & kBlockedControlBits; restore != 0) {
			// Silently too: an event now would carry a mask other systems never saw go
			// down, and nothing needs telling that the bits are back.
			WriteControlsSilently(controls, static_cast<UEFlag>(restore), true);
		}
		_savedControls = 0;
	}

}
