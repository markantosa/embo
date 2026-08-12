#include "mixing_options.h"
#include "config.h"

static SyringeAgent _agent = SyringeAgent::Gelfoam;
static SyringeType _syringetype = SyringeType::Terumo;
static TargetType   _targetType = TargetType::SIZE;
static float _viscosityTargetPaS = TARGET_VISCOSITY_PA_S_DEFAULT;

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

float mixing_options_get_viscosity_target_pa_s() { return _viscosityTargetPaS; }
void mixing_options_set_viscosity_target_pa_s(float paS) {
    if (paS < TARGET_VISCOSITY_PA_S_MIN) paS = TARGET_VISCOSITY_PA_S_MIN;
    if (paS > TARGET_VISCOSITY_PA_S_MAX) paS = TARGET_VISCOSITY_PA_S_MAX;
    _viscosityTargetPaS = paS;
}
