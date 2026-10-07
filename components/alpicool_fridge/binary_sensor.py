import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_CONNECTIVITY, ENTITY_CATEGORY_DIAGNOSTIC

from . import ALPICOOL_FRIDGE_COMPONENT_SCHEMA, CONF_ALPICOOL_FRIDGE_ID

DEPENDENCIES = ["alpicool_fridge"]

CONF_ONLINE = "online"
CONF_DUAL_ZONE = "dual_zone"

CONFIG_SCHEMA = ALPICOOL_FRIDGE_COMPONENT_SCHEMA.extend(
    {
        # True while connected and receiving status frames.
        cv.Optional(CONF_ONLINE): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_DUAL_ZONE): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ALPICOOL_FRIDGE_ID])
    if conf := config.get(CONF_ONLINE):
        cg.add(hub.set_online_binary_sensor(await binary_sensor.new_binary_sensor(conf)))
    if conf := config.get(CONF_DUAL_ZONE):
        cg.add(
            hub.set_dual_zone_binary_sensor(await binary_sensor.new_binary_sensor(conf))
        )
