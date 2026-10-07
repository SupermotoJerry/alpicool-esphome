import esphome.codegen as cg
from esphome.components import climate
import esphome.config_validation as cv

from . import (
    ALPICOOL_FRIDGE_COMPONENT_SCHEMA,
    CONF_ALPICOOL_FRIDGE_ID,
    ZONES,
    alpicool_fridge_ns,
)

DEPENDENCIES = ["alpicool_fridge"]

CONF_ZONE = "zone"

AlpicoolClimate = alpicool_fridge_ns.class_(
    "AlpicoolClimate", climate.Climate, cg.Component
)

CONFIG_SCHEMA = climate.climate_schema(AlpicoolClimate).extend(
    ALPICOOL_FRIDGE_COMPONENT_SCHEMA.extend(
        {
            cv.Optional(CONF_ZONE, default="left"): cv.one_of(*ZONES, lower=True),
        }
    )
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ALPICOOL_FRIDGE_ID])
    zone = ZONES[config[CONF_ZONE]]
    var = await climate.new_climate(config, hub, zone)
    await cg.register_component(var, config)
    cg.add(hub.set_climate(zone, var))
