import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = ["@dalklein"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor"]

solivia_ns = cg.esphome_ns.namespace("solivia")
Solivia = solivia_ns.class_("Solivia", cg.PollingComponent, uart.UARTDevice)

CONF_ADDRESS = "address"
CONF_BURST = "burst"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Solivia),
            # 255 is the SOLIVIA broadcast address and is refused outright.
            cv.Optional(CONF_ADDRESS, default=1): cv.int_range(min=1, max=254),
            # transactions issued back-to-back per update(); pair a V with its A
            cv.Optional(CONF_BURST, default=1): cv.int_range(min=1, max=6),
        }
    )
    .extend(cv.polling_component_schema("2s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(var.set_burst(config[CONF_BURST]))
