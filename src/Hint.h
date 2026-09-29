#pragma once

namespace ProjectileControl
{
	// The button row at the bottom of the HUD while an arrow is being flown:
	//   [W][A][S][D] Steer    [Space] Let go
	// drawn with the same keycap art the favourites menu uses under Untarnished UI.
	//
	// Technique from STB Hotkey System's WidgetKeycaps: on the HUD's own AdvanceMovie the
	// keycap symbol is imported once into the HUD movie's definition, then attached to a
	// clip of ours on _root and sent to the frame that matches the key -- keyboard
	// scancode, 256 + mouse button, 266 + gamepad button, SkyUI's ButtonArt layout.
	// Nothing in the game's SWFs is edited.
	//
	// The art is taken from whatever the installed interface already provides (see kSources
	// in the .cpp); the copy this mod ships is only the last resort.
	class Hint
	{
	public:
		static void Install();  // kDataLoaded
	};
}
