import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID

from . import Solivia, solivia_ns

DEPENDENCIES = ["solivia"]

CONF_SOLIVIA_ID = "solivia_id"
CONF_CMD = "cmd"
CONF_SUB = "sub"
CONF_DIVISOR = "divisor"


def _validate_read_only(value):
    """sub >= 0x80 sets the high bit, which is a WRITE on this protocol. Refuse at
    COMPILE time so a write can never be emitted, not merely avoided at runtime."""
    if value >= 0x80:
        raise cv.Invalid(
            f"sub 0x{value:02x} has the high bit set = WRITE. This component is "
            f"READ-ONLY by construction; writes to the inverter are refused."
        )
    return value


CONFIG_SCHEMA = sensor.sensor_schema().extend(
    {
        cv.GenerateID(CONF_SOLIVIA_ID): cv.use_id(Solivia),
        cv.Required(CONF_CMD): cv.int_range(min=0, max=255),
        cv.Required(CONF_SUB): cv.All(cv.int_range(min=0, max=255), _validate_read_only),
        cv.Optional(CONF_DIVISOR, default=1.0): cv.float_,
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SOLIVIA_ID])
    var = await sensor.new_sensor(config)
    cg.add(hub.add_sensor(config[CONF_CMD], config[CONF_SUB], config[CONF_DIVISOR], var))
