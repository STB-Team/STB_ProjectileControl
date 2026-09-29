#include "Input.h"

#include "Settings.h"

namespace ProjectileControl
{
	namespace
	{
		// Order matches Input::_moveKeys / _moveDown.
		enum Move : std::size_t
		{
			kForward = 0,
			kBack,
			kStrafeLeft,
			kStrafeRight
		};

		// Index into Input::_attackHeld for a button that attacks or casts, or -1.
		// Mouse buttons 0/1; gamepad triggers report as buttons 0x09 / 0x0A.
		int AttackIndex(RE::INPUT_DEVICE a_device, std::uint32_t a_code)
		{
			if (a_device == RE::INPUT_DEVICE::kMouse && a_code <= 1) {
				return static_cast<int>(a_code);
			}
			if (a_device == RE::INPUT_DEVICE::kGamepad && (a_code == 0x09 || a_code == 0x0A)) {
				return a_code == 0x09 ? 2 : 3;
			}
			return -1;
		}

		// The number pad's - and + stand in for the main row's, so either hand works.
		bool IsTimeKey(std::uint32_t a_code, std::uint32_t a_key)
		{
			return a_key != 0 && (a_code == a_key || (a_key == 0x0C && a_code == 0x4A) || (a_key == 0x0D && a_code == 0x4E));
		}
	}

	Input* Input::GetSingleton()
	{
		static Input singleton;
		return &singleton;
	}

	void Input::Register()
	{
		if (auto* idm = RE::BSInputDeviceManager::GetSingleton()) {
			idm->AddEventSink(GetSingleton());
			logger::info("input sink registered");
		} else {
			logger::error("no BSInputDeviceManager, the plugin stays inactive");
		}
	}

	void Input::BeginSession()
	{
		std::uint32_t keys[4]{ 0, 0, 0, 0 };
		if (auto* controls = RE::ControlMap::GetSingleton()) {
			if (const auto* events = RE::UserEvents::GetSingleton()) {
				const auto mapped = [&](const RE::BSFixedString& a_event) {
					return controls->GetMappedKey(a_event, RE::INPUT_DEVICE::kKeyboard,
						RE::ControlMap::InputContextID::kGameplay);
				};
				keys[kForward] = mapped(events->forward);
				keys[kBack] = mapped(events->back);
				keys[kStrafeLeft] = mapped(events->strafeLeft);
				keys[kStrafeRight] = mapped(events->strafeRight);
			}
		}
		for (std::size_t i = 0; i < 4; ++i) {
			// kInvalid means "unbound"; leave it at 0 so no scancode ever matches.
			if (keys[i] == RE::ControlMap::kInvalid) {
				keys[i] = 0;
			}
			_moveKeys[i].store(keys[i], std::memory_order_relaxed);
			_moveDown[i].store(false, std::memory_order_relaxed);
		}
		_steerX.store(0.f, std::memory_order_relaxed);
		_steerY.store(0.f, std::memory_order_relaxed);
		_mouseDX.store(0.f, std::memory_order_relaxed);
		_mouseDY.store(0.f, std::memory_order_relaxed);
		_lookX.store(0.f, std::memory_order_relaxed);
		_lookY.store(0.f, std::memory_order_relaxed);
		_timeSteps.store(0, std::memory_order_relaxed);
		// A press latched before the ride (the key happens to be the trigger, say) must not
		// end the ride on its first frame.
		_cancelPressed.store(false, std::memory_order_relaxed);
		_sessionActive.store(true, std::memory_order_relaxed);

		logger::debug("steering keys: fwd=0x{:X} back=0x{:X} left=0x{:X} right=0x{:X}", keys[kForward], keys[kBack],
			keys[kStrafeLeft], keys[kStrafeRight]);
	}

	bool Input::AttackBlocked()
	{
		if (_sessionActive.load(std::memory_order_relaxed)) {
			return true;
		}
		if (!_blockAfterRide.load(std::memory_order_relaxed)) {
			return false;
		}
		for (const auto& held : _attackHeld) {
			if (held.load(std::memory_order_relaxed)) {
				return true;
			}
		}
		// Everything is up: the next press is a fresh one and belongs to the game.
		_blockAfterRide.store(false, std::memory_order_relaxed);
		return false;
	}

	void Input::EndSession()
	{
		bool anyHeld = false;
		for (const auto& held : _attackHeld) {
			anyHeld = anyHeld || held.load(std::memory_order_relaxed);
		}
		_blockAfterRide.store(anyHeld, std::memory_order_relaxed);
		_sessionActive.store(false, std::memory_order_relaxed);
		_cancelPressed.store(false, std::memory_order_relaxed);
		for (auto& down : _moveDown) {
			down.store(false, std::memory_order_relaxed);
		}
		_steerX.store(0.f, std::memory_order_relaxed);
		_steerY.store(0.f, std::memory_order_relaxed);
		_mouseDX.store(0.f, std::memory_order_relaxed);
		_mouseDY.store(0.f, std::memory_order_relaxed);
		_lookX.store(0.f, std::memory_order_relaxed);
		_lookY.store(0.f, std::memory_order_relaxed);
		_timeSteps.store(0, std::memory_order_relaxed);
	}

	void Input::ConsumeMouse(float& a_dx, float& a_dy)
	{
		a_dx = _mouseDX.exchange(0.f, std::memory_order_relaxed);
		a_dy = _mouseDY.exchange(0.f, std::memory_order_relaxed);
	}

	void Input::UpdateAxes()
	{
		const auto down = [](std::size_t a_i) { return _moveDown[a_i].load(std::memory_order_relaxed) ? 1.f : 0.f; };
		_steerX.store(down(kStrafeRight) - down(kStrafeLeft), std::memory_order_relaxed);
		_steerY.store(down(kForward) - down(kBack), std::memory_order_relaxed);
	}

	RE::BSEventNotifyControl Input::ProcessEvent(
		RE::InputEvent* const*               a_event,
		RE::BSTEventSource<RE::InputEvent*>* /*a_source*/)
	{
		if (!a_event) {
			return RE::BSEventNotifyControl::kContinue;
		}

		const bool inSession = _sessionActive.load(std::memory_order_relaxed);
		bool       axesDirty = false;

		for (auto* event = *a_event; event; event = event->next) {
			switch (event->eventType.get()) {
			case RE::INPUT_EVENT_TYPE::kButton:
				{
					const auto* button = event->AsButtonEvent();
					if (!button) {
						break;
					}
					const auto code = button->GetIDCode();
					const auto device = button->GetDevice();

					if (const int attack = AttackIndex(device, code); attack >= 0) {
						_attackHeld[attack].store(button->IsPressed(), std::memory_order_relaxed);
					}

					// IsDown is the press edge only (held time still zero), so holding the key
					// from before the ride -- it may be the trigger itself -- never cancels.
					const auto cancelKey = device == RE::INPUT_DEVICE::kKeyboard ? Settings::CancelKeyboard() :
					                       device == RE::INPUT_DEVICE::kGamepad  ? Settings::CancelGamepad() :
					                                                               0u;
					if (inSession && cancelKey != 0 && code == cancelKey && button->IsDown()) {
						_cancelPressed.store(true, std::memory_order_relaxed);
						_usingGamepad.store(device == RE::INPUT_DEVICE::kGamepad, std::memory_order_relaxed);
					}

					// Tracked outside the ride too, so a button already down when the ride
					// starts (a crossbow fires on the press) is known to be held.
					if (const auto steerKey = Settings::MouseSteerKey(); steerKey != 0) {
						const bool mouse = device == RE::INPUT_DEVICE::kMouse;
						const auto asKey = mouse ? Settings::kMouseBase + code : code;
						if ((mouse || device == RE::INPUT_DEVICE::kKeyboard) && asKey == steerKey) {
							_mouseSteerHeld.store(button->IsPressed(), std::memory_order_relaxed);
						}
					}

					if (device == RE::INPUT_DEVICE::kKeyboard) {
						_usingGamepad.store(false, std::memory_order_relaxed);
						if (inSession && button->IsDown()) {
							if (IsTimeKey(code, Settings::SlowerKey())) {
								_timeSteps.fetch_sub(1, std::memory_order_relaxed);
							} else if (IsTimeKey(code, Settings::FasterKey())) {
								_timeSteps.fetch_add(1, std::memory_order_relaxed);
							}
						}
						if (const auto key = Settings::TriggerKeyboard(); key != 0 && code == key) {
							_triggerHeld.store(button->IsPressed(), std::memory_order_relaxed);
						}
						if (inSession) {
							for (std::size_t i = 0; i < 4; ++i) {
								if (const auto mapped = _moveKeys[i].load(std::memory_order_relaxed);
									mapped != 0 && code == mapped) {
									_moveDown[i].store(button->IsPressed(), std::memory_order_relaxed);
									axesDirty = true;
								}
							}
						}
					} else if (device == RE::INPUT_DEVICE::kGamepad) {
						_usingGamepad.store(true, std::memory_order_relaxed);
						if (const auto key = Settings::TriggerGamepad(); key != 0 && code == key) {
							_triggerHeld.store(button->IsPressed(), std::memory_order_relaxed);
						}
					}
				}
				break;

			case RE::INPUT_EVENT_TYPE::kThumbstick:
				_usingGamepad.store(true, std::memory_order_relaxed);
				if (inSession) {
					const auto* stick = static_cast<const RE::ThumbstickEvent*>(event);
					if (stick->IsLeft()) {
						_steerX.store(std::clamp(stick->xValue, -1.f, 1.f), std::memory_order_relaxed);
						_steerY.store(std::clamp(stick->yValue, -1.f, 1.f), std::memory_order_relaxed);
					} else {
						_lookX.store(std::clamp(stick->xValue, -1.f, 1.f), std::memory_order_relaxed);
						_lookY.store(std::clamp(stick->yValue, -1.f, 1.f), std::memory_order_relaxed);
					}
				}
				break;

			case RE::INPUT_EVENT_TYPE::kMouseMove:
				_usingGamepad.store(false, std::memory_order_relaxed);
				if (inSession && (Settings::MouseLook() || Settings::MouseSteerKey() != 0)) {
					const auto* mouse = static_cast<const RE::MouseMoveEvent*>(event);
					_mouseDX.store(_mouseDX.load(std::memory_order_relaxed) + static_cast<float>(mouse->mouseInputX),
						std::memory_order_relaxed);
					_mouseDY.store(_mouseDY.load(std::memory_order_relaxed) + static_cast<float>(mouse->mouseInputY),
						std::memory_order_relaxed);
				}
				break;

			default:
				break;
			}
		}

		if (axesDirty) {
			UpdateAxes();
		}
		return RE::BSEventNotifyControl::kContinue;
	}
}
