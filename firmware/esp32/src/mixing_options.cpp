#include "mixing_options.h"
#include "config.h"

static SyringeAgent _agent = SyringeAgent::Gelfoam;
static SyringeType _syringetype = SyringeType::Terumo;
static TargetType   _targetType = TargetType::SIZE;
static uint16_t _viscosityTargetCp = TARGET_VISCOSITY_CP_DEFAULT;

SyringeAgent mixing_options_get_agent() { return _agent; }
void mixing_options_set_agent(SyringeAgent agent) { _agent = agent; }
const char *mixing_options_agent_label() {
    return _agent == SyringeAgent::Gelfoam ? "Gelfoam" : "Lyostypt";
}
SyringeType mixing_options_get_syringe_type() {return _syringetype;}
void mixing_options_set_syringe_type(SyringeType syrtype) {_syringetype = syrtype;}
const char *mixing_options_set_syringe_type_label() {
    return _syringetype == SyringeType::Terumo ? "Terumo" : "Nipro";
}

TargetType mixing_options_get_target_type() { return _targetType; }
void mixing_options_set_target_type(TargetType type) { _targetType = type; }
const char *mixing_options_target_type_label() {
    return _targetType == TargetType::SIZE ? "Target size" : "Target viscosity";
}

uint16_t mixing_options_get_viscosity_target_cp() { return _viscosityTargetCp; }
void mixing_options_set_viscosity_target_cp(uint16_t cp) {
    if (cp < TARGET_VISCOSITY_CP_MIN) cp = TARGET_VISCOSITY_CP_MIN;
    if (cp > TARGET_VISCOSITY_CP_MAX) cp = TARGET_VISCOSITY_CP_MAX;
    _viscosityTargetCp = cp;
}
