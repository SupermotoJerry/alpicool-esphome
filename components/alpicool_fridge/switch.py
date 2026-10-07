import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import CONF_POWER, ENTITY_CATEGORY_CONFIG

from . import ALPICOOL_FRIDGE_COMPONENT_SCHEMA, CONF_ALPICOOL_FRIDGE_ID, alpicool_fridge_ns

DEPENDENCIES = ["alpicool_fridge"]

CONF_LOCK = "lock"

AlpicoolSwitch = alpicool_fridge_ns.class_("AlpicoolSwitch", switch.Switch, cg.Component)
SwitchType = alpicool_fridge_ns.enum("SwitchType")

SWITCHES = {
    CONF_POWER: (SwitchType.SWITCH_POWER, "mdi:power", cv.UNDEFINED),
    # Locks the buttons on the fridge's control panel.
    CONF_LOCK: (SwitchType.SWITCH_LOCK, "mdi:lock", ENTITY_CATEGORY_CONFIG),
}

CONFIG_SCHEMA = ALPICOOL_FRIDGE_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(key): switch.switch_schema(
            AlpicoolSwitch, icon=icon, entity_category=category
        )
        for key, (_, icon, category) in SWITCHES.items()
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ALPICOOL_FRIDGE_ID])
    for key, (switch_type, _, _) in SWITCHES.items():
        if conf := config.get(key):
            var = await switch.new_switch(conf, hub, switch_type)
            await cg.register_component(var, conf)
            cg.add(hub.set_switch(switch_type, var))
