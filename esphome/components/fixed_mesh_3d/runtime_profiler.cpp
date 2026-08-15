#include "runtime_profiler.h"

#include <algorithm>
#include <cinttypes>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::fixed_mesh_3d {

static constexpr const char *TAG = "fixed_mesh_cpu";

void RuntimeProfilerComponent::loop() {
#if defined(USE_ESP32) && defined(CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS) && !CONFIG_FREERTOS_SMP
  const uint32_t now = millis();
  if (this->last_sample_ms_ != 0 && now - this->last_sample_ms_ < 1000)
    return;
  this->last_sample_ms_ = now;
  this->sample_();
#endif
}

#if defined(USE_ESP32) && defined(CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS) && !CONFIG_FREERTOS_SMP

void RuntimeProfilerComponent::sample_() {
  const UBaseType_t task_count = uxTaskGetNumberOfTasks();
  if (task_count > MAX_TASKS) {
    ESP_LOGW(TAG, "Task profiler capacity %u is smaller than task count %u",
             static_cast<unsigned>(MAX_TASKS), static_cast<unsigned>(task_count));
    return;
  }

  configRUN_TIME_COUNTER_TYPE total_runtime = 0;
  const UBaseType_t count = uxTaskGetSystemState(this->current_tasks_.data(), MAX_TASKS, &total_runtime);
  if (count == 0) {
    ESP_LOGW(TAG, "uxTaskGetSystemState returned no task data");
    return;
  }

  const TaskHandle_t idle_handles[2] = {
      xTaskGetIdleTaskHandleForCore(0),
      xTaskGetIdleTaskHandleForCore(1),
  };
  const configRUN_TIME_COUNTER_TYPE idle_runtime[2] = {
      ulTaskGetIdleRunTimeCounterForCore(0),
      ulTaskGetIdleRunTimeCounterForCore(1),
  };

  if (this->primed_) {
    const configRUN_TIME_COUNTER_TYPE total_delta = total_runtime - this->previous_total_runtime_;
    if (total_delta != 0) {
      const auto busy_x10 = [total_delta](configRUN_TIME_COUNTER_TYPE idle_delta) -> uint32_t {
        const uint64_t idle_x10 = std::min<uint64_t>(
            1000, (static_cast<uint64_t>(idle_delta) * 1000ULL) / total_delta);
        return static_cast<uint32_t>(1000ULL - idle_x10);
      };

      const uint32_t core0_x10 = busy_x10(idle_runtime[0] - this->previous_idle_runtime_[0]);
      const uint32_t core1_x10 = busy_x10(idle_runtime[1] - this->previous_idle_runtime_[1]);
      const uint32_t total_x10 = (core0_x10 + core1_x10) / 2;

      size_t delta_count = 0;
      for (UBaseType_t i = 0; i < count && delta_count < MAX_TASKS; i++) {
        const auto &current = this->current_tasks_[i];
        if (current.xHandle == idle_handles[0] || current.xHandle == idle_handles[1])
          continue;
        for (size_t j = 0; j < this->previous_task_count_; j++) {
          if (this->previous_tasks_[j].handle != current.xHandle)
            continue;
          // Task handles can be recycled. Treat a counter that moved backwards
          // as a new task instead of turning the unsigned subtraction into an
          // enormous bogus CPU spike.
          if (current.ulRunTimeCounter < this->previous_tasks_[j].runtime)
            break;
          const configRUN_TIME_COUNTER_TYPE delta = current.ulRunTimeCounter - this->previous_tasks_[j].runtime;
          if (delta != 0) {
            auto &entry = this->task_deltas_[delta_count++];
            entry.name = current.pcTaskName;
            entry.handle = current.xHandle;
            entry.runtime = delta;
            entry.affinity = xTaskGetCoreID(current.xHandle);
          }
          break;
        }
      }

      std::sort(this->task_deltas_.begin(), this->task_deltas_.begin() + delta_count,
                [](const TaskDelta &a, const TaskDelta &b) { return a.runtime > b.runtime; });

      ESP_LOGI(TAG, "CPU0 %" PRIu32 ".%" PRIu32 "%% | CPU1 %" PRIu32 ".%" PRIu32
                    "%% | total %" PRIu32 ".%" PRIu32 "%% | tasks %u",
               core0_x10 / 10, core0_x10 % 10, core1_x10 / 10, core1_x10 % 10,
               total_x10 / 10, total_x10 % 10, static_cast<unsigned>(count));

      const size_t log_count = std::min(delta_count, MAX_LOG_TASKS);
      for (size_t i = 0; i < log_count; i++) {
        const auto &entry = this->task_deltas_[i];
        const uint32_t cpu_x10 = static_cast<uint32_t>(
            std::min<uint64_t>(2000, (static_cast<uint64_t>(entry.runtime) * 1000ULL) / total_delta));
        if (cpu_x10 == 0)
          continue;
        const int affinity = entry.affinity == tskNO_AFFINITY ? -1 : static_cast<int>(entry.affinity);
        // ESP_TIMER is explicitly selected as the runtime-stat source, so the
        // scheduler counter unit is one microsecond.
        const uint64_t runtime_ms = static_cast<uint64_t>(entry.runtime) / 1000ULL;
        ESP_LOGI(TAG, "  %-16s core %d | %" PRIu32 ".%" PRIu32 "%% core | +%" PRIu64 " ms",
                 entry.name != nullptr ? entry.name : "?", affinity,
                 cpu_x10 / 10, cpu_x10 % 10, runtime_ms);
      }
    }
  }

  this->previous_total_runtime_ = total_runtime;
  this->previous_idle_runtime_[0] = idle_runtime[0];
  this->previous_idle_runtime_[1] = idle_runtime[1];
  this->previous_task_count_ = std::min<size_t>(count, MAX_TASKS);
  for (size_t i = 0; i < this->previous_task_count_; i++) {
    this->previous_tasks_[i].handle = this->current_tasks_[i].xHandle;
    this->previous_tasks_[i].runtime = this->current_tasks_[i].ulRunTimeCounter;
  }
  this->primed_ = true;
}

#endif

}  // namespace esphome::fixed_mesh_3d
