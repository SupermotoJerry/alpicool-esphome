import esphome.codegen as cg
from esphome.components import ble_client
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["ble_client"]
AUTO_LOAD = ["binary_sensor", "climate", "number", "select", "sensor", "switch"]
MULTI_CONF = True

CONF_ALPICOOL_FRIDGE_ID = "alpicool_fridge_id"
CONF_BIND_ON_CONNECT = "bind_on_connect"
CONF_STATUS_TIMEOUT = "status_timeout"

alpicool_fridge_ns = cg.esphome_ns.namespace("alpicool_fridge")
AlpicoolFridge = alpicool_fridge_ns.class_(
    "AlpicoolFridge", cg.PollingComponent, ble_client.BLEClientNode
)
Zone = alpicool_fridge_ns.enum("Zone")
ZONES = {"left": Zone.ZONE_LEFT, "right": Zone.ZONE_RIGHT}

ALPICOOL_FRIDGE_COMPONENT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ALPICOOL_FRIDGE_ID): cv.use_id(AlpicoolFridge),
    }
)

CONFIG_SCHEMA = cv.All(
    cv.only_on_esp32,
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AlpicoolFridge),
            # Bind makes the fridge show "APP" and wait for a button press. The
            # protocol works without it, so it's off for unattended use.
            cv.Optional(CONF_BIND_ON_CONNECT, default=False): cv.boolean,
            # Values go unavailable after this long without a status.
            cv.Optional(
                CONF_STATUS_TIMEOUT, default="120s"
            ): cv.positive_time_period_milliseconds,
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(cv.polling_component_schema("30s")),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    cg.add(var.set_bind_on_connect(config[CONF_BIND_ON_CONNECT]))
    cg.add(var.set_status_timeout(config[CONF_STATUS_TIMEOUT]))
