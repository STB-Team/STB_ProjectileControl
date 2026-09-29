#pragma once

namespace ProjectileControl
{
	// Every hook is a vtable write through CommonLibSSE-NG's VTABLE_ constants, so the same
	// DLL works on SE 1.5.97, AE 1.6.x and 1.7.x without a per-runtime address table. What
	// each slot does is explained next to it in Hooks.cpp.
	class Hooks
	{
	public:
		static void Install();
	};
}
