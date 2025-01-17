#include <database.hpp>
#include <functional>
#include <memory.hpp>
#include <queue.hpp>
#include <semaphore.hpp>
#include <term.hpp>
#include <thread.hpp>
#include <timer.hpp>

#include "om.hpp"

namespace System {
template <typename RobotType, typename... RobotParam>
void Start(RobotParam... param) {
  auto init_fun = [](RobotParam... param) {
    Message* msg = static_cast<Message*>(pvPortMalloc(sizeof(Message)));
    new (msg) Message();
    Term* term = static_cast<Term*>(pvPortMalloc(sizeof(Term)));
    new (term) Term();
    Database* database = static_cast<Database*>(pvPortMalloc(sizeof(Database)));
    new (database) Database();
    Timer* timer = static_cast<Timer*>(pvPortMalloc(sizeof(Timer)));
    new (timer) Timer();

    RobotType robot(param...);

    while (1) {
      System::Thread::Sleep(UINT32_MAX);
    }
  };

  std::function<void(void)>* init_fun_call =
      new std::function<void(void)>(std::bind(init_fun, param...));

  auto init_thread_fn = [](std::function<void(void)>* init_fun) {
    (*init_fun)();
  };

  System::Thread init_thread;

  init_thread.Create(init_thread_fn, init_fun_call, "init_thread_fn",
                     INIT_TASK_STACK_DEPTH, System::Thread::REALTIME);

  if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
    vTaskStartScheduler();
  }
}
}  // namespace System
