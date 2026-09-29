#include "Hooks.h"

#include "Input.h"
#include "Session.h"
#include "Settings.h"

namespace ProjectileControl
{
	namespace
	{
		// ---- ArrowProjectile / MissileProjectile ------------------------------------
		//
		// MissileProjectile::UpdateImpl ends in Projectile::Move, which reads the velocity
		// back through the virtual GetLinearVelocity, scales it by the frame delta and
		// moves the reference. Bending linearVelocity here, before the original runs, sets
		// the path. The model's orientation is NOT derived from it -- Session::Steer writes
		// the reference angles as well, see there.
		//
		// The same two hooks go on ArrowProjectile and on MissileProjectile. They do not
		// overlap: ArrowProjectile::UpdateImpl calls MissileProjectile::UpdateImpl directly,
		// not through the vtable, so an arrow never passes through the missile hook. One
		// instantiation per kind, since each slot keeps its own original.
		template <Session::Kind K>
		struct UpdateImpl
		{
			static void thunk(RE::Projectile* a_this, float a_delta)
			{
				if (a_this) {
					if (Session::IsTracked(a_this)) {
						Session::Steer(a_this);
					} else {
						Session::TryCapture(a_this, K);
					}
				}
				func(a_this, a_delta);
			}

			static inline REL::Relocation<decltype(thunk)> func;
			static constexpr std::size_t                   idx = 0xAB;
		};

		// MissileProjectile slot 0xC3 (CommonLib: Unk_C3), read by MissileProjectile::UpdateImpl
		// and handed to Projectile::Sync3DWithReference as canReorientTracer. With it set,
		// every frame the model's rotation is rebuilt from the reference angles; without it
		// the model keeps the orientation it was fired with. ArrowProjectile returns 1 --
		// which is why writing angle.x/z was enough for arrows -- MissileProjectile returns
		// 0, so a steered spell flew on pointing the way it was cast. Returning 1 for the
		// tracked spell makes it take the angles Session::Steer writes, like an arrow.
		// Only the missile vtable needs it: arrows already return 1.
		struct CanReorient
		{
			static bool thunk(RE::Projectile* a_this)
			{
				return Session::IsTracked(a_this) ? true : func(a_this);
			}

			static inline REL::Relocation<decltype(thunk)> func;
			static constexpr std::size_t                   idx = 0xC3;
		};

		// There is deliberately no ProcessImpacts hook. That function runs on every frame,
		// not on a hit -- the engine gates the real work on the impact list being non-empty
		// and flags the projectile kProcessedImpacts afterwards. The flag is the honest
		// signal and Session checks it directly; hooking the call ended every ride on its
		// first frame.

		template <Session::Kind K>
		struct GetGravity
		{
			static float thunk(RE::Projectile* a_this)
			{
				const float gravity = func(a_this);
				return Session::IsTracked(a_this) ? gravity * Session::GravityScale() : gravity;
			}

			static inline REL::Relocation<decltype(thunk)> func;
			static constexpr std::size_t                   idx = 0xB5;
		};

		// ---- FreeCameraState ----------------------------------------------------------
		//
		// FreeCameraState::Update is the only consumer of these two: it calls GetRotation,
		// then GetTranslation, and writes both straight into the camera root's local
		// transform. Overriding the getters rather than the fields is what makes the
		// override stick -- Update runs UpdateMovement first, which would otherwise
		// overwrite anything written to translation beforehand.

		struct GetRotation
		{
			static void thunk(RE::FreeCameraState* a_this, RE::NiQuaternion* a_rotation)
			{
				const bool ours = a_this && Session::IsActive() && Session::PrepareCamera(a_this);
				func(a_this, a_rotation);
				if (ours && a_rotation) {
					Session::WriteRotation(*a_rotation);
				}
			}

			static inline REL::Relocation<decltype(thunk)> func;
			static constexpr std::size_t                   idx = 0x04;
		};

		struct GetTranslation
		{
			static void thunk(RE::FreeCameraState* a_this, RE::NiPoint3* a_translation)
			{
				if (a_this && Session::IsActive()) {
					Session::ApplyCameraTranslation(a_this);
				}
				func(a_this, a_translation);
			}

			static inline REL::Relocation<decltype(thunk)> func;
			static constexpr std::size_t                   idx = 0x05;
		};

		// ---- AttackBlockHandler -------------------------------------------------------
		//
		// The player-controls handler for attacking, casting and blocking. The ride switches
		// fighting off in the control mask, which stops the attack itself -- but the handler
		// still took the presses in and remembered them: holding a mouse button to steer
		// drew the bow or charged the spell, and it went off the moment control came back.
		// Refusing it the input altogether, for the ride and until every attack button is
		// up again, means nothing is remembered.
		struct AttackCanProcess
		{
			static bool thunk(RE::PlayerInputHandler* a_this, RE::InputEvent* a_event)
			{
				return Input::AttackBlocked() ? false : func(a_this, a_event);
			}

			static inline REL::Relocation<decltype(thunk)> func;
			static constexpr std::size_t                   idx = 0x01;
		};

		// ---- PlayerCharacter ----------------------------------------------------------

		struct PlayerUpdate
		{
			static void thunk(RE::PlayerCharacter* a_this, float a_delta)
			{
				func(a_this, a_delta);
				Session::Watchdog();
			}

			static inline REL::Relocation<decltype(thunk)> func;
			static constexpr std::size_t                   idx = 0xAD;
		};

		template <class T, const auto& VTable>
		void WriteVFunc()
		{
			REL::Relocation<std::uintptr_t> vtable{ VTable[0] };
			T::func = vtable.write_vfunc(T::idx, T::thunk);
		}
	}

	void Hooks::Install()
	{
		WriteVFunc<UpdateImpl<Session::Kind::kArrow>, RE::VTABLE_ArrowProjectile>();
		WriteVFunc<GetGravity<Session::Kind::kArrow>, RE::VTABLE_ArrowProjectile>();
		WriteVFunc<UpdateImpl<Session::Kind::kMissile>, RE::VTABLE_MissileProjectile>();
		WriteVFunc<GetGravity<Session::Kind::kMissile>, RE::VTABLE_MissileProjectile>();
		WriteVFunc<CanReorient, RE::VTABLE_MissileProjectile>();
		WriteVFunc<GetRotation, RE::VTABLE_FreeCameraState>();
		WriteVFunc<GetTranslation, RE::VTABLE_FreeCameraState>();
		WriteVFunc<PlayerUpdate, RE::VTABLE_PlayerCharacter>();
		WriteVFunc<AttackCanProcess, RE::VTABLE_AttackBlockHandler>();

		logger::info(
			"hooks installed (arrow + missile update/gravity, free camera rotation/translation, player update, attack input)");
	}
}
