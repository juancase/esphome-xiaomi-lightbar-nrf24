import esphome.codegen as cg
from esphome.components import button, datetime, light, number, switch
from esphome.components import time as time_
import esphome.config_validation as cv
from esphome.const import (
    CONF_ENTITY_CATEGORY,
    CONF_HOUR,
    CONF_INITIAL_VALUE,
    CONF_MINUTE,
    CONF_OUTPUT_ID,
    CONF_SECOND,
    CONF_TIME,
    CONF_TIME_ID,
    ENTITY_CATEGORY_CONFIG,
    UNIT_MINUTE,
)

from . import (
    CONF_SERIAL,
    CONF_XIAOMI_LIGHTBAR_ID,
    SERIAL_SCHEMA,
    XiaomiLightbar,
    xiaomi_lightbar_ns,
)

DEPENDENCIES = ["xiaomi_lightbar"]
AUTO_LOAD = ["button", "datetime", "number", "switch"]

CONF_AUTO_CALIBRATE = "auto_calibrate"
CONF_CALIBRATE_OFF = "calibrate_off"
CONF_CALIBRATE_ON = "calibrate_on"
CONF_ENABLED = "enabled"
CONF_PAIR = "pair"
CONF_QUIET_MINUTES = "quiet_minutes"
CONF_REMOTES = "remotes"
CONF_RESEND = "resend"

MAX_QUIET_MINUTES = 24 * 60

XiaomiLightbarLight = xiaomi_lightbar_ns.class_(
    "XiaomiLightbarLight", cg.Component, light.LightOutput
)
LightButton = xiaomi_lightbar_ns.class_("LightButton", button.Button)
LightAction = xiaomi_lightbar_ns.enum("LightAction", is_class=True)
AutoCalibrateSwitch = xiaomi_lightbar_ns.class_(
    "AutoCalibrateSwitch", switch.Switch, cg.Component
)
AutoCalibrateTime = xiaomi_lightbar_ns.class_(
    "AutoCalibrateTime", datetime.TimeEntity, cg.Component
)
AutoCalibrateNumber = xiaomi_lightbar_ns.class_(
    "AutoCalibrateNumber", number.Number, cg.Component
)

# Buttons of a light: config key, action, icon.
BUTTONS = [
    # Not needed if the bar is already paired with `serial` (e.g. it is the remote's).
    (CONF_PAIR, LightAction.PAIR, "mdi:link-variant"),
    # Calibration: tell the controller what the bar really shows, without sending anything.
    (CONF_CALIBRATE_ON, LightAction.CALIBRATE_ON, "mdi:lightbulb-on-outline"),
    (CONF_CALIBRATE_OFF, LightAction.CALIBRATE_OFF, "mdi:lightbulb-off-outline"),
    # Calibration: set brightness and color temperature again, absolutely.
    (CONF_RESEND, LightAction.RESEND, "mdi:refresh"),
]

# Once a day, assume the bar is off (it usually is at night), so a state that got out of step is
# corrected. Postponed while the bar is used. All three settings can be changed from Home Assistant.
AUTO_CALIBRATE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_TIME_ID): cv.use_id(time_.RealTimeClock),
        cv.Required(CONF_ENABLED): switch.switch_schema(
            AutoCalibrateSwitch,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:calendar-sync",
            default_restore_mode="RESTORE_DEFAULT_ON",
        ).extend(cv.COMPONENT_SCHEMA),
        cv.Required(CONF_TIME): datetime.time_schema(AutoCalibrateTime)
        .extend(
            {
                cv.Optional(CONF_ENTITY_CATEGORY, default=ENTITY_CATEGORY_CONFIG): cv.entity_category,
                cv.Optional(CONF_INITIAL_VALUE, default="04:00:00"): cv.date_time(
                    date=False, time=True
                ),
            }
        )
        .extend(cv.COMPONENT_SCHEMA),
        # Minutes without use (Home Assistant or remote) before the calibration runs.
        cv.Required(CONF_QUIET_MINUTES): number.number_schema(
            AutoCalibrateNumber,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:timer-sand",
            unit_of_measurement=UNIT_MINUTE,
        )
        .extend(
            {
                cv.Optional(CONF_INITIAL_VALUE, default=60): cv.int_range(
                    min=0, max=MAX_QUIET_MINUTES
                ),
            }
        )
        .extend(cv.COMPONENT_SCHEMA),
    }
)

# BINARY schema on purpose: no gamma correction and no default transition. The bar fades on its
# own, and ESPHome transitions would only produce intermediate values that get merged anyway.
CONFIG_SCHEMA = light.light_schema(
    XiaomiLightbarLight,
    light.LightType.BINARY,
    default_restore_mode="RESTORE_DEFAULT_OFF",
).extend(
    {
        cv.GenerateID(CONF_XIAOMI_LIGHTBAR_ID): cv.use_id(XiaomiLightbar),
        # The serial the bar obeys: its remote's, or any other value after pairing.
        cv.Required(CONF_SERIAL): SERIAL_SCHEMA,
        # Remotes the bar is not paired with. Their commands are passed on to the bar.
        cv.Optional(CONF_REMOTES, default=[]): cv.ensure_list(SERIAL_SCHEMA),
        **{
            cv.Optional(key): button.button_schema(
                LightButton, entity_category=ENTITY_CATEGORY_CONFIG, icon=icon
            )
            for key, _, icon in BUTTONS
        },
        cv.Optional(CONF_AUTO_CALIBRATE): AUTO_CALIBRATE_SCHEMA,
    }
).extend(cv.COMPONENT_SCHEMA)


def _validate_remotes(config):
    remotes = config[CONF_REMOTES]
    if len(set(remotes)) != len(remotes):
        raise cv.Invalid("Each remote can only be listed once", path=[CONF_REMOTES])
    if config[CONF_SERIAL] in config[CONF_REMOTES]:
        raise cv.Invalid(
            f"The serial 0x{config[CONF_SERIAL]:06X} is the bar's own: its commands are always "
            f"tracked, remove it from '{CONF_REMOTES}'",
            path=[CONF_REMOTES],
        )
    return config


CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, _validate_remotes)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    await light.register_light(var, config)

    hub = await cg.get_variable(config[CONF_XIAOMI_LIGHTBAR_ID])
    cg.add(var.set_hub(hub))
    cg.add(var.set_serial(config[CONF_SERIAL]))
    for remote in config[CONF_REMOTES]:
        cg.add(var.add_remote(remote))

    for key, action, _ in BUTTONS:
        if button_config := config.get(key):
            btn = await button.new_button(button_config)
            cg.add(btn.set_parent(var))
            cg.add(btn.set_action(action))

    if auto_config := config.get(CONF_AUTO_CALIBRATE):
        cg.add_define("USE_XIAOMI_LIGHTBAR_AUTO_CALIBRATE")
        clock = await cg.get_variable(auto_config[CONF_TIME_ID])

        enabled_config = auto_config[CONF_ENABLED]
        enabled = await switch.new_switch(enabled_config)
        await cg.register_component(enabled, enabled_config)
        cg.add(enabled.set_parent(var))

        time_config = auto_config[CONF_TIME]
        at = await datetime.new_datetime(time_config)
        await cg.register_component(at, time_config)
        cg.add(at.set_parent(var))
        initial = time_config[CONF_INITIAL_VALUE]
        cg.add(
            at.set_initial_value(
                cg.StructInitializer(
                    cg.ESPTime,
                    ("second", initial[CONF_SECOND]),
                    ("minute", initial[CONF_MINUTE]),
                    ("hour", initial[CONF_HOUR]),
                )
            )
        )

        quiet_config = auto_config[CONF_QUIET_MINUTES]
        quiet = await number.new_number(
            quiet_config, min_value=0, max_value=MAX_QUIET_MINUTES, step=1
        )
        await cg.register_component(quiet, quiet_config)
        cg.add(quiet.set_initial_value(quiet_config[CONF_INITIAL_VALUE]))

        cg.add(var.set_auto_calibrate(clock, enabled, at, quiet))
