import esphome.config_validation as cv
from esphome.components.esp32 import add_idf_sdkconfig_option
from esphome.core import CORE

CODEOWNERS = []
DEPENDENCIES = ["esp32"]
CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    # Fixed-mesh experiments use FreeRTOS task/core runtime deltas to decide
    # where renderer workers should live. Match Espressif's real_time_stats
    # example and use 64-bit counters so long-running nodes do not wrap.
    if CORE.using_esp_idf:
        add_idf_sdkconfig_option("CONFIG_FREERTOS_USE_TRACE_FACILITY", True)
        add_idf_sdkconfig_option("CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS", True)
        add_idf_sdkconfig_option("CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U64", True)
