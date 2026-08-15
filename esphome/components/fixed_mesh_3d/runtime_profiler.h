#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "esphome/core/component.h"

#if defined(USE_ESP32)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#endif

namespace esphome::fixed_mesh_3d {

class RuntimeProfilerComponent : public Component {
 public:
  void loop() override;

 protected:
#if defined(USE_ESP32) && defined(CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS) && !CONFIG_FREERTOS_SMP
  static constexpr size_t MAX_TASKS = 48;
  static constexpr size_t MAX_LOG_TASKS = 8;

  struct PreviousTaskSample {
    TaskHandle_t handle{nullptr};
    configRUN_TIME_COUNTER_TYPE runtime{0};
  };

  struct TaskDelta {
    const char *name{nullptr};
    TaskHandle_t handle{nullptr};
    configRUN_TIME_COUNTER_TYPE runtime{0};
    BaseType_t affinity{tskNO_AFFINITY};
  };

  void sample_();

  uint32_t last_sample_ms_{0};
  bool primed_{false};
  configRUN_TIME_COUNTER_TYPE previous_total_runtime_{0};
  configRUN_TIME_COUNTER_TYPE previous_idle_runtime_[2]{0, 0};
  std::array<PreviousTaskSample, MAX_TASKS> previous_tasks_{};
  size_t previous_task_count_{0};
  std::array<TaskStatus_t, MAX_TASKS> current_tasks_{};
  std::array<TaskDelta, MAX_TASKS> task_deltas_{};
#endif
};

}  // namespace esphome::fixed_mesh_3d
