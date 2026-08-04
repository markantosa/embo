#pragma once

#include <stdint.h>

// Two selections made upstream of the Mixing Menu (Start > Agent selection
// > Target type > Mixing Menu). Both are cosmetic/labeling only for now,
// same product decision already made for the syringe preset when it lived
// inside Mixing Menu directly — see mixing_menu_screen.cpp. Neither changes
// any calibration constant or introduces a real separate viscosity unit;
// there is no viscosity calibration anywhere in this codebase, so
// selecting "Viscosity" here only changes Mixing Menu's on-screen label,
// not the underlying control, which stays the same µm target either way.

enum class SyringeAgent : uint8_t { TERUMO, NIPRO };
enum class TargetType   : uint8_t { SIZE, VISCOSITY };

SyringeAgent mixing_options_get_agent();
void         mixing_options_set_agent(SyringeAgent agent);
const char  *mixing_options_agent_label();

TargetType   mixing_options_get_target_type();
void         mixing_options_set_target_type(TargetType type);
const char  *mixing_options_target_type_label();
