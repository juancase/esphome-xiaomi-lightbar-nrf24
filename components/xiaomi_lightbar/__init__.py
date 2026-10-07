from esphome import pins
import esphome.codegen as cg
from esphome.components import spi
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["spi"]
MULTI_CONF = True

CONF_CE_PIN = "ce_pin"
CONF_SERIAL = "serial"
CONF_XIAOMI_LIGHTBAR_ID = "xiaomi_lightbar_id"

# The 3 byte ID of a remote.
SERIAL_SCHEMA = cv.hex_int_range(min=0, max=0xFFFFFF)

xiaomi_lightbar_ns = cg.esphome_ns.namespace("xiaomi_lightbar")
XiaomiLightbar = xiaomi_lightbar_ns.class_(
    "XiaomiLightbar", cg.Component, spi.SPIDevice
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(XiaomiLightbar),
            cv.Required(CONF_CE_PIN): pins.gpio_output_pin_schema,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=True))
)

FINAL_VALIDATE_SCHEMA = spi.final_validate_device_schema(
    "xiaomi_lightbar", require_mosi=True, require_miso=True
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)

    ce_pin = await cg.gpio_pin_expression(config[CONF_CE_PIN])
    cg.add(var.set_ce_pin(ce_pin))
