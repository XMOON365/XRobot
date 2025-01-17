#include <cstdlib>
#include <term.hpp>
#include <thread.hpp>

#include "bsp_usb.h"
#include "ms.h"
#include "om.hpp"

using namespace System;

static System::Thread term_thread, usb_thread;

static ms_item_t log_control, task_info;

static bool log_enable = false;

#ifdef MCU_DEBUG_BUILD
static char task_print_buff[1024];
#endif

extern ms_t ms;

static int log_ctrl_fn(ms_item_t *item, int argc, char **argv) {
  MS_UNUSED(item);
  if (argc == 1) {
    printf("on开启/off关闭\r\n");
  } else if (argc == 2) {
    if (strcmp(argv[1], "on") == 0) {
      ms_clear();
      log_enable = true;
    } else if (strcmp(argv[1], "off") == 0) {
      log_enable = false;
    } else {
      printf("命令错误。\r\n");
    }
  } else {
    printf("命令错误。\r\n");
  }

  return 0;
}

static om_status_t print_log(om_msg_t *msg, void *arg) {
  (void)arg;

  if (!log_enable) {
    return OM_OK;
  }

  om_log_t *log = static_cast<om_log_t *>(msg->buff);

  printf("%-.4f %s", bsp_time_get(), log->data);

  return OM_OK;
}

static int term_write(const char *data, uint32_t len) {
  if (log_enable) {
    return OM_OK;
  }
  bsp_usb_transmit(reinterpret_cast<const uint8_t *>(data), len);
  return static_cast<int>(len);
}

int printf(const char *format, ...) {
  va_list v_arg_list;
  va_start(v_arg_list, format);
  (void)vsnprintf(ms.buff.write_buff, sizeof(ms.buff.write_buff), format,
                  v_arg_list);
  va_end(v_arg_list);

  bsp_usb_transmit(reinterpret_cast<const uint8_t *>(ms.buff.write_buff),
                   strlen(ms.buff.write_buff));

  return 0;
}

Term::Term() {
  bsp_usb_init();

  ms_init(term_write);

  ms_file_init(&log_control, "log", log_ctrl_fn, NULL, NULL);
  ms_cmd_add(&log_control);

  om_config_topic(om_get_log_handle(), "d", print_log, NULL);

#ifdef MCU_DEBUG_BUILD

  auto task_cmd_fn = [](ms_item_t *item, int argc, char **argv) {
    (void)item;
    (void)argc;
    (void)argv;

    vTaskList(task_print_buff);
    printf("Name            State   Pri     Stack   Num\r\n");
    bsp_usb_transmit(reinterpret_cast<const uint8_t *>(task_print_buff),
                     strnlen(task_print_buff, sizeof(task_print_buff)));

    return 0;
  };

  ms_file_init(&task_info, "task_info", task_cmd_fn, NULL, NULL);
  ms_cmd_add(&task_info);

#endif

  auto usb_thread_fn = [](void *arg) {
    (void)arg;
    while (1) {
      bsp_usb_update();
      vTaskDelay(10);
    }
  };

  usb_thread.Create(usb_thread_fn, static_cast<void *>(0), "usb_thread",
                    FREERTOS_USB_TASK_STACK_DEPTH, System::Thread::REALTIME);

  auto term_thread_fn = [](void *arg) {
    (void)arg;
    while (1) {
      while (!bsp_usb_connect()) {
        term_thread.Sleep(1);
      }

      ms_start();

      while (1) {
        if (bsp_usb_avail()) {
          ms_input(bsp_usb_read_char());
        }
        if (!bsp_usb_connect()) {
          break;
        }
        vTaskDelay(10);
      }
    }
  };

  term_thread.Create(term_thread_fn, static_cast<void *>(0), "term_thread",
                     FREERTOS_TERM_TASK_STACK_DEPTH, System::Thread::LOW);
}
