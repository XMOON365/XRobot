#include <device.hpp>

namespace Device {
class WearLab {
 public:
  typedef struct __attribute__((packed)) {
    uint32_t id;
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    uint8_t data[8];
    uint8_t end;
  } UartData;

  typedef struct __attribute__((packed)) {
    uint32_t time;
    uint8_t device_id : 6;      //设备ID
    uint8_t area_id;            //区域ID
    uint16_t device_type : 12;  //设备类型
    uint8_t data_type : 6;      //数据类型
                                // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    uint8_t data[8];            //数据
  } UdpData;

  typedef union {
    struct __attribute__((packed)) {
      uint32_t device_type : 12;
      uint32_t data_type : 6;
      uint32_t device_id : 6;
      uint32_t res : 8;
    } data;

    uint32_t raw;
  } CanHeader;

  typedef struct __attribute__((packed)) {
    double data1;
  } CanData1;

  typedef struct __attribute__((packed)) {
    float data1;
    float data2;
  } CanData2;

  typedef struct __attribute__((packed)) {
    int32_t data1_symbol : 1;
    uint32_t data1 : 20;
    int32_t data2_symbol : 1;
    uint32_t data2 : 20;
    int32_t data3_symbol : 1;
    uint32_t data3 : 20;
    int32_t res : 1;
  } CanData3;

  typedef struct __attribute__((packed)) {
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    int16_t data[4];
  } CanData4;
};
}  // namespace Device
