#pragma once

#include <stdint.h>

// Two selections made upstream of the Mixing Menu (Start > Agent selection > syringe selection 
// > Target type > Mixing Menu / Viscosity Menu). The syringe preset stays
// cosmetic/labeling only, per the original product decision (see
// mixing_menu_screen.h) — but Target Type is NOT cosmetic: Size and
// Viscosity runs use genuinely different equations and stop conditions
// (scheduler.cpp) and different running screens
// (mixing_running_screen.h / viscosity_mixing_running_screen.h). This
// file just stores the selections themselves; the equations and control
// logic live in calibration.h/scheduler.cpp.

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

// Viscosity target, in Pa*s (matching calib_estimate_viscosity_pa_s()'s
// native output unit directly — was centipoise before v0.7.5). Clamped to
// TARGET_VISCOSITY_PA_S_MIN/MAX (config.h) by the setter, same pattern as
// scheduler_set_target_um().
float mixing_options_get_viscosity_target_pa_s();
void  mixing_options_set_viscosity_target_pa_s(float paS);