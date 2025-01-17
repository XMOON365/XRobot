#include "robot.hpp"

#include "system.hpp"

/* clang-format off */
Robot::NetConfig::Param param = {
  .led = {
    .gpio = BSP_GPIO_LED,
    .timeout = 200,
  },

  .topic_share = {
    .topic_name = {"net_info"},
    .block = true,
    .uart = BSP_UART_1,
    .cycle = 10,
  }
};
/* clang-format on */

void robot_init() {
  System::Start<Robot::NetConfig, Robot::NetConfig::Param>(param);
}
