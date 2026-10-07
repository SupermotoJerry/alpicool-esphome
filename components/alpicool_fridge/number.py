import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import (
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_STEP,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_CONFIG,
    UNIT_CELSIUS,
    UNIT_MINUTE,
)

from . import ALPICOOL_FRIDGE_COMPONENT_SCHEMA, CONF_ALPICOOL_FRIDGE_ID, alpicool_fridge_ns

DEPENDENCIES = ["alpicool_fridge"]

AlpicoolNumber = alpicool_fridge_ns.class_("AlpicoolNumber", number.Number, cg.Component)
NumberType = alpicool_fridge_ns.enum("NumberType")

# key: (type, min, max, unit, device class, icon, entity category)
NUMBERS = {
    # Target temperatures, as an alternative to the climate entities that is
    # simpler to drive over plain MQTT. The fridge clamps to its own range.
    "left_target_temperature": (
        NumberType.NUMBER_LEFT_TARGET, -20, 20, UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE,
        "mdi:thermometer", cv.UNDEFINED,
    ),
    "right_target_temperature": (
        NumberType.NUMBER_RIGHT_TARGET, -20, 20, UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE,
        "mdi:thermometer", cv.UNDEFINED,
    ),
    # Temperature differences, in the unit the fridge is set to.
    "left_hysteresis": (
        NumberType.NUMBER_LEFT_HYSTERESIS, 1, 10, UNIT_CELSIUS, cv.UNDEFINED,
        "mdi:thermometer-lines", ENTITY_CATEGORY_CONFIG,
    ),
    "right_hysteresis": (
        NumberType.NUMBER_RIGHT_HYSTERESIS, 1, 10, UNIT_CELSIUS, cv.UNDEFINED,
        "mdi:thermometer-lines", ENTITY_CATEGORY_CONFIG,
    ),
    # Compressor start delay.
    "start_delay": (
        NumberType.NUMBER_START_DELAY, 0, 10, UNIT_MINUTE, cv.UNDEFINED,
        "mdi:timer-outline", ENTITY_CATEGORY_CONFIG,
    ),
}


def _number_schema(min_value, max_value, unit, device_class, icon, category):
    return number.number_schema(
        AlpicoolNumber,
        unit_of_measurement=unit,
        device_class=device_class,
        icon=icon,
        entity_category=category,
    ).extend(
        {
            cv.Optional(CONF_MIN_VALUE, default=min_value): cv.float_,
            cv.Optional(CONF_MAX_VALUE, default=max_value): cv.float_,
            cv.Optional(CONF_STEP, default=1): cv.positive_float,
        }
    )


CONFIG_SCHEMA = ALPICOOL_FRIDGE_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(key): _number_schema(*spec[1:])
        for key, spec in NUMBERS.items()
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ALPICOOL_FRIDGE_ID])
    for key, (number_type, *_) in NUMBERS.items():
        if conf := config.get(key):
            var = await number.new_number(
                conf,
                hub,
                number_type,
                min_value=conf[CONF_MIN_VALUE],
                max_value=conf[CONF_MAX_VALUE],
                step=conf[CONF_STEP],
            )
            await cg.register_component(var, conf)
            cg.add(hub.set_number(number_type, var))
