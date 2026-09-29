#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Scaleform vtables for the SWF import helper (swfhelper/), which the HUD hint uses to
// bring the keycap symbol into the HUD movie.
#include "RE/Offsets.Ext.h"

#pragma warning(push)
#include <spdlog/sinks/basic_file_sink.h>
#pragma warning(pop)

using namespace std::literals;

namespace logger = SKSE::log;

#define DLLEXPORT __declspec(dllexport)
