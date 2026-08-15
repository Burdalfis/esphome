import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.esp32 import (
    VARIANT_ESP32S3,
    add_idf_sdkconfig_option,
    get_esp32_variant,
)
from esphome.const import CONF_ID
from esphome.core import CORE

CODEOWNERS = []
DEPENDENCIES = ["esp32"]

fixed_mesh_3d_ns = cg.esphome_ns.namespace("fixed_mesh_3d")
RuntimeProfilerComponent = fixed_mesh_3d_ns.class_("RuntimeProfilerComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RuntimeProfilerComponent),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    # On the dual-core S3, collect FreeRTOS task/core runtime deltas so renderer
    # worker placement can be based on measured CPU occupancy. Match Espressif's
    # real_time_stats example and use ESP_TIMER + 64-bit counters.
    if CORE.target_framework == "esp-idf" and get_esp32_variant() == VARIANT_ESP32S3:
        add_idf_sdkconfig_option("CONFIG_FREERTOS_USE_TRACE_FACILITY", True)
        add_idf_sdkconfig_option("CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS", True)
        add_idf_sdkconfig_option("CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U64", True)
        add_idf_sdkconfig_option("CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER", True)

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
