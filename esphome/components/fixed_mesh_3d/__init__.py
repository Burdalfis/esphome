import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.esp32 import add_idf_sdkconfig_option
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
    # Fixed-mesh experiments use FreeRTOS task/core runtime deltas to decide
    # where renderer workers should live. Match Espressif's real_time_stats
    # example and use ESP_TIMER + 64-bit counters for stable time-based data.
    if CORE.using_esp_idf:
        add_idf_sdkconfig_option("CONFIG_FREERTOS_USE_TRACE_FACILITY", True)
        add_idf_sdkconfig_option("CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS", True)
        add_idf_sdkconfig_option("CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U64", True)
        add_idf_sdkconfig_option("CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER", True)

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
