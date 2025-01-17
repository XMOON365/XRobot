#pragma once

#include <cstdint>
#include <string>

#include "FreeRTOS.h"
#include "bsp_time.h"
#include "system_ext.hpp"
#include "task.h"

namespace System {
class Thread {
 public:
  typedef enum { IDLE, LOW, MEDIUM, HIGH, REALTIME } Priority;

  template <typename FunType, typename ArgType>
  void Create(FunType fun, ArgType arg, const char* name, uint32_t stack_depth,
              Priority priority) {
    (void)static_cast<void (*)(ArgType)>(fun);

    TypeErasure<void, ArgType>* type = static_cast<TypeErasure<void, ArgType>*>(
        pvPortMalloc(sizeof(TypeErasure<void, ArgType>)));

    *type = TypeErasure<void, ArgType>(fun, arg);

    xTaskCreate(type->Port, name, stack_depth, type, priority,
                &(this->handle_));
  }

  static void Sleep(uint32_t microseconds) { vTaskDelay(microseconds); }

  void SleepUntil(uint32_t microseconds) {
    vTaskDelayUntil(&last_weakup_tick_, microseconds);
    last_weakup_tick_ = xTaskGetTickCount();
  }

  void Stop() { vTaskSuspend(this->handle_); }

 private:
  TaskHandle_t handle_ = NULL;
  uint32_t last_weakup_tick_ = bsp_time_get_ms();
};
}  // namespace System
