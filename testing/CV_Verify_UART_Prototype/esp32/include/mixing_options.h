#pragma once

#include <stdint.h>

// Two selections made upstream of the Mixing Menu (Start > Agent selection > syringe selection 
// > Target type > Mixing Menu). Both are cosmetic/labeling only for now,
// same product decision already made for the syringe preset when it lived
// inside Mixing Menu directly — see mixing_menu_screen.cpp. Neither changes
// any calibration constant or introduces a real separate viscosity unit;
// there is no viscosity calibration anywhere in this codebase, so
// selecting "Viscosity" here only changes Mixing Menu's on-screen label,
// not the underlying control, which stays the same µm target either way.

enum class SyringeAgent : uint8_t { Gelfoam, Lyostypt };
enum class SyringeType  : uint8_t { Terumo , Nipro};
enum class TargetType   : uint8_t { SIZE, VISCOSITY };

SyringeAgent mixing_options_get_agent();
void         mixing_options_set_agent(SyringeAgent agent);
const char  *mixing_options_agent_label();

SyringeType mixing_options_get_syringe_type();
void        mixing_options_set_syringe_type(SyringeType syrtype);
const char *mixing_options_set_syringe_type_label();

TargetType   mixing_options_get_target_type();
void         mixing_options_set_target_type(TargetType type);
const char  *mixing_options_target_type_label();