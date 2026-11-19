import esphome.codegen as cg
from esphome.components import binary_sensor, remote_base, sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_CHANNEL,
    CONF_ID,
    CONF_TEMPERATURE,
    CONF_TIMEOUT,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
)

DEPENDENCIES = ["remote_receiver"]
AUTO_LOAD = ["remote_base", "sensor", "binary_sensor"]

CONF_SENSORS = "sensors"
CONF_BATTERY_LOW = "battery_low"
MAX_CHANNELS = 8

amir_ns = cg.esphome_ns.namespace("amir_thermometer")
AmirThermometer = amir_ns.class_(
    "AmirThermometer", cg.Component, remote_base.RemoteReceiverListener
)

CHANNEL_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_CHANNEL): cv.int_range(min=1, max=MAX_CHANNELS),
        cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_BATTERY_LOW): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_BATTERY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


def _unique_channels(value):
    channels = [item[CONF_CHANNEL] for item in value]
    if len(channels) != len(set(channels)):
        raise cv.Invalid("Each channel may only be listed once")
    return value


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(AmirThermometer),
        cv.GenerateID(remote_base.CONF_RECEIVER_ID): cv.use_id(
            remote_base.RemoteReceiverBase
        ),
        cv.Optional(CONF_SENSORS, default=[]): cv.All(
            cv.ensure_list(CHANNEL_SCHEMA), _unique_channels
        ),
        # Temperature goes "unknown" if a sensor is silent this long (it sends every ~50 s).
        cv.Optional(CONF_TIMEOUT, default="5min"): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await remote_base.register_listener(var, config)
    cg.add(var.set_timeout(config[CONF_TIMEOUT]))

    for item in config[CONF_SENSORS]:
        channel = item[CONF_CHANNEL]
        if temp_conf := item.get(CONF_TEMPERATURE):
            sens = await sensor.new_sensor(temp_conf)
            cg.add(var.set_temperature_sensor(channel, sens))
        if batt_conf := item.get(CONF_BATTERY_LOW):
            bsens = await binary_sensor.new_binary_sensor(batt_conf)
            cg.add(var.set_battery_low_sensor(channel, bsens))
