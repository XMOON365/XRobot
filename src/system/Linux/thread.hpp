#pragma once

#include <poll.h>
#include <pthread.h>
#include <stdint.h>

#include <string>

#include "bsp_time.h"
#include "system_ext.hpp"

namespace System {
class Thread {
 public:
  typedef enum { IDLE, LOW, MEDIUM, HIGH, REALTIME } Priority;

  template <typename FunType, typename ArgType>
  void Create(FunType fun, ArgType arg, const char* name, uint32_t stack_depth,
              Priority priority) {
    (void)name;
    (void)stack_depth;
    (void)priority;

    (void)static_cast<void (*)(ArgType)>(fun);
    TypeErasure<void, ArgType>* type = static_cast<TypeErasure<void, ArgType>*>(
        malloc(sizeof(TypeErasure<void, ArgType>)));

    *type = TypeErasure<void, ArgType>(fun, arg);

    auto port = [](void* arg) {
      TypeErasure<void, ArgType>* type =
          static_cast<TypeErasure<void, ArgType>*>(arg);
      type->fun_(type->arg_);
      return static_cast<void*>(NULL);
    };

    pthread_create(&this->handle_, NULL, port, type);
  }

  static void Sleep(uint32_t microseconds) { poll(NULL, 0, microseconds); }

  void SleepUntil(uint32_t microseconds) {
    last_weakup_tick_ += microseconds;
    while (bsp_time_get_ms() < last_weakup_tick_) {
      poll(NULL, 0, 1);
    }
  }

  void Stop() { pthread_cancel(this->handle_); }

 private:
  pthread_t handle_;
  uint32_t last_weakup_tick_ = bsp_time_get_ms();
};
}  // namespace System
