#include "can_usart.hpp"
#include "dev_can.hpp"

void robot_init();

namespace Robot {
class CanToUart {
 public:
  typedef struct {
  } Param;

  Device::Can can_;
  Module::CantoUsart can_uart_;
  CanToUart(Param& param) : can_uart_() { (void)param; }
};
}  // namespace Robot
