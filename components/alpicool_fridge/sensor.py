import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_THERMOMETER,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
    UNIT_VOLT,
)

from . import ALPICOOL_FRIDGE_COMPONENT_SCHEMA, CONF_ALPICOOL_FRIDGE_ID, ZONES

DEPENDENCIES = ["alpicool_fridge"]

CONF_CURRENT_TEMPERATURE = "current_temperature"
CONF_TARGET_TEMPERATURE = "target_temperature"
CONF_BATTERY_PERCENT = "battery_percent"
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_RUNNING_STATUS = "running_status"

TEMPERATURE_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_CELSIUS,
    icon=ICON_THERMOMETER,
    accuracy_decimals=0,
    device_class=DEVICE_CLASS_TEMPERATURE,
    state_class=STATE_CLASS_MEASUREMENT,
)

ZONE_SENSOR_KEYS = {
    f"{zone}_{key}": (zone, key)
    for zone in ZONES
    for key in (CONF_CURRENT_TEMPERATURE, CONF_TARGET_TEMPERATURE)
}

CONFIG_SCHEMA = ALPICOOL_FRIDGE_COMPONENT_SCHEMA.extend(
    {
        **{cv.Optional(key): TEMPERATURE_SCHEMA for key in ZONE_SENSOR_KEYS},
        cv.Optional(CONF_BATTERY_PERCENT): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_BATTERY,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_BATTERY_VOLTAGE): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        # Raw byte 27 of a dual-zone status; its meaning is not known yet.
        cv.Optional(CONF_RUNNING_STATUS): sensor.sensor_schema(
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ALPICOOL_FRIDGE_ID])
    for key, (zone, kind) in ZONE_SENSOR_KEYS.items():
        if conf := config.get(key):
            sens = await sensor.new_sensor(conf)
            setter = (
                hub.set_current_temperature_sensor
                if kind == CONF_CURRENT_TEMPERATURE
                else hub.set_target_temperature_sensor
            )
            cg.add(setter(ZONES[zone], sens))
    if conf := config.get(CONF_BATTERY_PERCENT):
        cg.add(hub.set_battery_percent_sensor(await sensor.new_sensor(conf)))
    if conf := config.get(CONF_BATTERY_VOLTAGE):
        cg.add(hub.set_battery_voltage_sensor(await sensor.new_sensor(conf)))
    if conf := config.get(CONF_RUNNING_STATUS):
        cg.add(hub.set_running_status_sensor(await sensor.new_sensor(conf)))
