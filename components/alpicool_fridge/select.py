import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import CONF_OPTIONS, ENTITY_CATEGORY_CONFIG

from . import ALPICOOL_FRIDGE_COMPONENT_SCHEMA, CONF_ALPICOOL_FRIDGE_ID, alpicool_fridge_ns

DEPENDENCIES = ["alpicool_fridge"]

CONF_RUN_MODE = "run_mode"
CONF_BATTERY_SAVER = "battery_saver"

AlpicoolSelect = alpicool_fridge_ns.class_("AlpicoolSelect", select.Select, cg.Component)
SelectType = alpicool_fridge_ns.enum("SelectType")

# Option N is sent to the fridge as value N. Some dual-zone models use run mode
# for Fridge/Freezer instead of Max/Eco, so the labels can be overridden.
SELECTS = {
    CONF_RUN_MODE: (SelectType.SELECT_RUN_MODE, ["Max", "Eco"], 2, "mdi:snowflake", cv.UNDEFINED),
    # Low-voltage cut-off level.
    CONF_BATTERY_SAVER: (
        SelectType.SELECT_BATTERY_SAVER,
        ["Low", "Medium", "High"],
        3,
        "mdi:car-battery",
        ENTITY_CATEGORY_CONFIG,
    ),
}


def _select_schema(defaults, count, icon, category):
    return select.select_schema(AlpicoolSelect, icon=icon, entity_category=category).extend(
        {
            cv.Optional(CONF_OPTIONS, default=defaults): cv.All(
                cv.ensure_list(cv.string_strict), cv.Length(min=count, max=count)
            ),
        }
    )


CONFIG_SCHEMA = ALPICOOL_FRIDGE_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(key): _select_schema(defaults, count, icon, category)
        for key, (_, defaults, count, icon, category) in SELECTS.items()
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ALPICOOL_FRIDGE_ID])
    for key, (select_type, *_) in SELECTS.items():
        if conf := config.get(key):
            var = await select.new_select(conf, hub, select_type, options=conf[CONF_OPTIONS])
            await cg.register_component(var, conf)
            cg.add(hub.set_select(select_type, var))
