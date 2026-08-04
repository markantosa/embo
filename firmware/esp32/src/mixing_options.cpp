#include "mixing_options.h"

static SyringeAgent _agent = SyringeAgent::TERUMO;
static TargetType   _targetType = TargetType::SIZE;

SyringeAgent mixing_options_get_agent() { return _agent; }
void mixing_options_set_agent(SyringeAgent agent) { _agent = agent; }
const char *mixing_options_agent_label() {
    return _agent == SyringeAgent::TERUMO ? "Terumo" : "Nipro";
}

TargetType mixing_options_get_target_type() { return _targetType; }
void mixing_options_set_target_type(TargetType type) { _targetType = type; }
const char *mixing_options_target_type_label() {
    return _targetType == TargetType::SIZE ? "Target size" : "Target viscosity";
}
