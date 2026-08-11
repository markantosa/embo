#pragma once

#include <stdint.h>

// Two selections made upstream of the Mixing Menu (Start > Agent selection > syringe selection 
// > Target type > Mixing Menu / Viscosity Menu). Both remain cosmetic/
// labeling only for now, same product decision already made for the
// syringe preset. Viscosity Selection now has its own genuinely separate
// stored value (mixing_options_get/set_viscosity_target()) — but there is
// still no viscosity calibration or measurement anywhere in this
// firmware, so it isn't wired into any control logic. Mixing started from
// either Size or Viscosity Selection runs the exact same way — the actual
// stop condition (scheduler.cpp) is still purely the UAS-voltage
// particle-size equation regardless of which target type was selected.

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

// Viscosity target — stored/displayed only, see the file comment above
// for why this doesn't drive any real control logic yet. Clamped to
// TARGET_VISCOSITY_CP_MIN/MAX (config.h) by the setter, same pattern as
// scheduler_set_target_um().
uint16_t mixing_options_get_viscosity_target_cp();
void     mixing_options_set_viscosity_target_cp(uint16_t cp);