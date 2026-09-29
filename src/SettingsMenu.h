#pragma once

namespace ProjectileControl
{
	// Every INI setting as a live page in SKSE Menu Framework ("STB Projectile Control" in its
	// list). Optional: without Data/SKSE/Plugins/SKSEMenuFramework.dll nothing registers
	// and the plugin runs on the INI alone.
	//
	// Threads. The pages are drawn on the render thread while the game reads the same
	// values on the main thread. Plain numbers and flags are written in place, as STB Item
	// Spin does -- aligned 4-byte stores, the worst a reader sees is last frame's value.
	// Anything that is not a plain number goes to the main thread as an SKSE task: the
	// always-ride list is an unordered_set the projectile hook walks, and a perk is a form
	// lookup. The INI is written from there too, and only once a slider is let go.
	class SettingsMenu
	{
	public:
		static void Register();  // kDataLoaded, after Settings::Load

	private:
		static void __stdcall RenderControls();
		static void __stdcall RenderProjectiles();
		static void __stdcall RenderCamera();
		static void __stdcall RenderSteering();
		static void __stdcall RenderImpactAndTime();
		static void __stdcall RenderCost();
		static void __stdcall RenderGeneral();

		static void Changed(bool a_changed);
	};
}
