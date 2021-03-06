#include "rule_descr.hpp"

namespace horizon {
const std::map<RuleID, RuleDescription> rule_descriptions = {
        {RuleID::HOLE_SIZE, {"Hole size", true, true}},
        {RuleID::TRACK_WIDTH, {"Track width", true, true}},
        {RuleID::CLEARANCE_COPPER, {"Copper clearance", true, true}},
        {RuleID::CLEARANCE_SILKSCREEN_EXPOSED_COPPER, {"Clearance\nSilkscreen - Exposed copper", false, false}},
        {RuleID::PARAMETERS, {"Parameters", false, false}},
        {RuleID::SINGLE_PIN_NET, {"Single pin nets", false, true}},
        {RuleID::VIA, {"Vias", true, false}},
        {RuleID::CLEARANCE_COPPER_OTHER, {"Clearance Copper - Other", true, true}},
        {RuleID::PLANE, {"Planes", true, false}},
        {RuleID::DIFFPAIR, {"Diffpair", true, false}},
        {RuleID::PACKAGE_CHECKS, {"Package checks", false, true}},
        {RuleID::PREFLIGHT_CHECKS, {"Preflight checks", false, true}},
        {RuleID::CLEARANCE_COPPER_KEEPOUT, {"Clearance Copper - Keepout", true, true}},
};
}
