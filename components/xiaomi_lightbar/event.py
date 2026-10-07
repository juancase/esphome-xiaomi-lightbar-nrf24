import esphome.codegen as cg
from esphome.components import event
import esphome.config_validation as cv

from . import (
    CONF_SERIAL,
    CONF_XIAOMI_LIGHTBAR_ID,
    SERIAL_SCHEMA,
    XiaomiLightbar,
    xiaomi_lightbar_ns,
)

DEPENDENCIES = ["xiaomi_lightbar"]

XiaomiLightbarRemote = xiaomi_lightbar_ns.class_(
    "XiaomiLightbarRemote", event.Event, cg.Component
)

EVENT_TYPES = [
    "press",
    "press_rotate_right",
    "press_rotate_left",
    "rotate_right",
    "rotate_left",
    "hold",
]

CONFIG_SCHEMA = (
    event.event_schema(XiaomiLightbarRemote, icon="mdi:remote")
    .extend(
        {
            cv.GenerateID(CONF_XIAOMI_LIGHTBAR_ID): cv.use_id(XiaomiLightbar),
            cv.Required(CONF_SERIAL): SERIAL_SCHEMA,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await event.new_event(config, event_types=EVENT_TYPES)
    await cg.register_component(var, config)

    hub = await cg.get_variable(config[CONF_XIAOMI_LIGHTBAR_ID])
    cg.add(var.set_hub(hub))
    cg.add(var.set_serial(config[CONF_SERIAL]))
