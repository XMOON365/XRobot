/*
  裁判系统抽象。
*/

#include "dev_referee.h"

#include <string.h>

#include "bsp_delay.h"
#include "bsp_uart.h"
#include "comp_crc16.h"
#include "comp_crc8.h"
#include "comp_utils.h"
#include "protocol.h"

#define REF_HEADER_SOF (0xA5)
#define REF_LEN_RX_BUFF (0xFF)
#define REF_LEN_TX_BUFF (0xFF)

#define REF_UI_BOX_UP_OFFSET (4)
#define REF_UI_BOX_BOT_OFFSET (-14)

#define REF_UI_RIGHT_START_W (0.85f)

#define REF_UI_MODE_LINE1_H (0.7f)
#define REF_UI_MODE_LINE2_H (0.68f)
#define REF_UI_MODE_LINE3_H (0.66f)
#define REF_UI_MODE_LINE4_H (0.64f)

#define REF_UI_MODE_OFFSET_1_LEFT (-6)
#define REF_UI_MODE_OFFSET_1_RIGHT (44)
#define REF_UI_MODE_OFFSET_2_LEFT (54)
#define REF_UI_MODE_OFFSET_2_RIGHT (102)
#define REF_UI_MODE_OFFSET_3_LEFT (114)
#define REF_UI_MODE_OFFSET_3_RIGHT (162)
#define REF_UI_MODE_OFFSET_4_LEFT (174)
#define REF_UI_MODE_OFFSET_4_RIGHT (222)

typedef struct __packed {
  referee_header_t header;
  uint16_t cmd_id;
  referee_inter_student_header_t student_header;
} referee_ui_packet_head_t;

static uint8_t rxbuf[REF_LEN_RX_BUFF];
static uint8_t txbuf[REF_LEN_TX_BUFF];

static referee_trans_t *gref_trans;
static referee_recv_t *gref_recv;

static bool trans_inited = false;
static bool recv_inited = false;

/* Private function  -------------------------------------------------------- */

static void referee_rx_cplt_callback(void *arg) {
  referee_recv_t *ref = arg;
  BaseType_t switch_required;
  xSemaphoreGiveFromISR(ref->sem.raw_ready, &switch_required);
  portYIELD_FROM_ISR(switch_required);
}

static void referee_tx_cplt_callback(void *arg) {
  referee_trans_t *ref = arg;
  BaseType_t switch_required;
  xSemaphoreGiveFromISR(ref->sem.packet_sent, &switch_required);
  portYIELD_FROM_ISR(switch_required);
}

static void referee_idle_line_callback(void *arg) {
  RM_UNUSED(arg);
  HAL_UART_AbortReceive_IT(bsp_uart_get_handle(BSP_UART_REF));
}

static void referee_abort_rx_cplt_callback(void *arg) {
  referee_recv_t *ref = arg;
  BaseType_t switch_required;
  xSemaphoreGiveFromISR(ref->sem.raw_ready, &switch_required);
  portYIELD_FROM_ISR(switch_required);
}

#if !UI_MODE_NONE
static void referee_fast_refresh_timer_callback(TimerHandle_t arg) {
  RM_UNUSED(arg);
  BaseType_t switch_required;
  xSemaphoreGiveFromISR(gref_trans->sem.ui_fast_refresh, &switch_required);
  portYIELD_FROM_ISR(switch_required);
}

static void referee_slow_refresh_timer_callback(TimerHandle_t arg) {
  RM_UNUSED(arg);
  BaseType_t switch_required;
  xSemaphoreGiveFromISR(gref_trans->sem.ui_slow_refresh, &switch_required);
  portYIELD_FROM_ISR(switch_required);
}
#endif

static int8_t referee_set_packet_header(referee_header_t *header,
                                        uint16_t data_length) {
  static uint8_t seq = 0;
  header->sof = REF_HEADER_SOF;
  header->data_length = data_length;
  header->seq = seq++;
  header->crc8 =
      crc8_calc((const uint8_t *)header,
                sizeof(referee_header_t) - sizeof(uint8_t), CRC8_INIT);
  return DEVICE_OK;
}

static int8_t referee_set_ui_header(referee_inter_student_header_t *header,
                                    const referee_student_cmd_id_t cmd_id,
                                    referee_robot_id_t robot_id) {
  header->cmd_id = cmd_id;
  header->id_sender = robot_id;
  if (robot_id > 100) {
    header->id_receiver = robot_id - 101 + 0x0165;
  } else {
    header->id_receiver = robot_id + 0x0100;
  }
  return DEVICE_OK;
}

int8_t referee_recv_init(referee_recv_t *ref) {
  ASSERT(ref);
  if (recv_inited) return DEVICE_ERR_INITED;

  memset(ref, 0, sizeof(*ref));

  gref_recv = ref;

  VERIFY((gref_recv->thread_alert = xTaskGetCurrentTaskHandle()) != NULL);

  ref->sem.raw_ready = xSemaphoreCreateBinary();

  bsp_uart_register_callback(BSP_UART_REF, BSP_UART_RX_CPLT_CB,
                             referee_rx_cplt_callback, ref);
  bsp_uart_register_callback(BSP_UART_REF, BSP_UART_ABORT_RX_CPLT_CB,
                             referee_abort_rx_cplt_callback, ref);
  bsp_uart_register_callback(BSP_UART_REF, BSP_UART_IDLE_LINE_CB,
                             referee_idle_line_callback, ref);

  __HAL_UART_ENABLE_IT(bsp_uart_get_handle(BSP_UART_REF), UART_IT_IDLE);

  recv_inited = true;
  return DEVICE_OK;
}

int8_t referee_trans_init(referee_trans_t *ref, const ui_screen_t *screen) {
  ASSERT(ref);

  if (trans_inited) return DEVICE_ERR_INITED;

  memset(ref, 0, sizeof(*ref));

  gref_trans = ref;

  VERIFY((gref_trans->thread_alert = xTaskGetCurrentTaskHandle()) != NULL);

  ref->ui.screen = screen;

  ref->sem.packet_sent = xSemaphoreCreateBinary();
  ref->sem.ui_fast_refresh = xSemaphoreCreateBinary();
  ref->sem.ui_slow_refresh = xSemaphoreCreateBinary();

  xSemaphoreGive(ref->sem.packet_sent);

  bsp_uart_register_callback(BSP_UART_REF, BSP_UART_TX_CPLT_CB,
                             referee_tx_cplt_callback, ref);
#if !UI_MODE_NONE
  ref->ui_fast_timer_id =
      xTimerCreate("fast_refresh", pdMS_TO_TICKS(UI_DYNAMIC_CYCLE), pdTRUE,
                   NULL, referee_fast_refresh_timer_callback);

  ref->ui_slow_timer_id =
      xTimerCreate("slow_refresh", pdMS_TO_TICKS(UI_STATIC_CYCLE), pdTRUE, NULL,
                   referee_slow_refresh_timer_callback);

  xTimerStart(ref->ui_fast_timer_id, pdMS_TO_TICKS(UI_DYNAMIC_CYCLE));
  xTimerStart(ref->ui_slow_timer_id, pdMS_TO_TICKS(UI_STATIC_CYCLE));
#endif

  __HAL_UART_ENABLE_IT(bsp_uart_get_handle(BSP_UART_REF), UART_IT_IDLE);

  trans_inited = true;
  return DEVICE_OK;
}

int8_t referee_restart(void) {
  __HAL_UART_DISABLE(bsp_uart_get_handle(BSP_UART_REF));
  __HAL_UART_ENABLE(bsp_uart_get_handle(BSP_UART_REF));
  return DEVICE_OK;
}

void referee_handle_offline(referee_recv_t *ref) {
  ref->status = REF_STATUS_OFFLINE;
}

int8_t referee_start_receiving(referee_recv_t *ref) {
  RM_UNUSED(ref);
  if (bsp_uart_receive(BSP_UART_REF, rxbuf, REF_LEN_RX_BUFF, false) == HAL_OK) {
    return DEVICE_OK;
  }
  return DEVICE_ERR;
}

bool referee_wait_recv_cplt(referee_recv_t *ref, uint32_t timeout) {
  return xSemaphoreTake(ref->sem.raw_ready, pdMS_TO_TICKS(timeout)) == pdTRUE;
}

int8_t referee_parse(referee_recv_t *ref) {
  ref->status = REF_STATUS_RUNNING;
  uint32_t data_length =
      REF_LEN_RX_BUFF -
      __HAL_DMA_GET_COUNTER(bsp_uart_get_handle(BSP_UART_REF)->hdmarx);

  const uint8_t *index = rxbuf; /* const 保护原始rxbuf不被修改 */
  const uint8_t *const rxbuf_end = rxbuf + data_length;

  while (index < rxbuf_end) {
    /* 1.处理帧头 */
    /* 1.1遍历所有找到SOF */
    while ((*index != REF_HEADER_SOF) && (index < rxbuf_end)) {
      index++;
    }
    /* 1.2将剩余数据当做帧头部 */
    referee_header_t *header = (referee_header_t *)index;

    /* 1.3验证完整性 */
    if (!crc8_verify((uint8_t *)header, sizeof(*header))) {
      index++;
      continue;
    }
    index += sizeof(*header);

    /* 2.处理CMD ID */
    /* 2.1将剩余数据当做CMD ID处理 */
    referee_cmd_id_t *cmd_id = (referee_cmd_id_t *)index;
    index += sizeof(*cmd_id);

    /* 3.处理数据段 */
    const void *source = index;
    void *destination;
    size_t size;

    switch (*cmd_id) {
      case REF_CMD_ID_GAME_STATUS:
        destination = &(ref->game_status);
        size = sizeof(ref->game_status);
        break;
      case REF_CMD_ID_GAME_RESULT:
        destination = &(ref->game_result);
        size = sizeof(ref->game_result);
        break;
      case REF_CMD_ID_GAME_ROBOT_HP:
        destination = &(ref->game_robot_hp);
        size = sizeof(ref->game_robot_hp);
        break;
      case REF_CMD_ID_DART_STATUS:
        destination = &(ref->dart_status);
        size = sizeof(ref->dart_status);
        break;
      case REF_CMD_ID_ICRA_ZONE_STATUS:
        destination = &(ref->icra_zone);
        size = sizeof(ref->icra_zone);
        break;
      case REF_CMD_ID_FIELD_EVENTS:
        destination = &(ref->field_event);
        size = sizeof(ref->field_event);
        break;
      case REF_CMD_ID_SUPPLY_ACTION:
        destination = &(ref->supply_action);
        size = sizeof(ref->supply_action);
        break;
      case REF_CMD_ID_WARNING:
        destination = &(ref->warning);
        size = sizeof(ref->warning);
        break;
      case REF_CMD_ID_DART_COUNTDOWN:
        destination = &(ref->dart_countdown);
        size = sizeof(ref->dart_countdown);
        break;
      case REF_CMD_ID_ROBOT_STATUS:
        destination = &(ref->robot_status);
        size = sizeof(ref->robot_status);
        break;
      case REF_CMD_ID_POWER_HEAT_DATA:
        destination = &(ref->power_heat);
        size = sizeof(ref->power_heat);
        break;
      case REF_CMD_ID_ROBOT_POS:
        destination = &(ref->robot_pos);
        size = sizeof(ref->robot_pos);
        break;
      case REF_CMD_ID_ROBOT_BUFF:
        destination = &(ref->robot_buff);
        size = sizeof(ref->robot_buff);
        break;
      case REF_CMD_ID_DRONE_ENERGY:
        destination = &(ref->drone_energy);
        size = sizeof(ref->drone_energy);
        break;
      case REF_CMD_ID_ROBOT_DMG:
        destination = &(ref->robot_danage);
        size = sizeof(ref->robot_danage);
        break;
      case REF_CMD_ID_LAUNCHER_DATA:
        destination = &(ref->launcher_data);
        size = sizeof(ref->launcher_data);
        break;
      case REF_CMD_ID_BULLET_REMAINING:
        destination = &(ref->bullet_remain);
        size = sizeof(ref->bullet_remain);
        break;
      case REF_CMD_ID_RFID:
        destination = &(ref->rfid);
        size = sizeof(ref->rfid);
        break;
      case REF_CMD_ID_DART_CLIENT:
        destination = &(ref->dart_client);
        size = sizeof(ref->dart_client);
        break;
      case REF_CMD_ID_CLIENT_MAP:
        destination = &(ref->client_map);
        size = sizeof(ref->client_map);
        break;
      case REF_CMD_ID_KEYBOARD_MOUSE:
        destination = &(ref->keyboard_mouse);
        size = sizeof(ref->keyboard_mouse);
        break;
      default:
        return DEVICE_ERR;
    }
    index += size;

    /* 4.处理帧尾 */
    index += sizeof(referee_tail_t);

    /* 验证无误则接受数据 */
    if (crc16_verify((uint8_t *)header, (uint8_t)(index - (uint8_t *)header)))
      memcpy(destination, source, size);
  }
#if REF_VIRTUAL
#if REF_FORCE_ONLINE
  ref->status = REF_STATUS_RUNNING;
#endif
  ref->power_heat.launcher_id1_17_heat = REF_HEAT_LIMIT_17;
  ref->power_heat.launcher_42_heat = REF_HEAT_LIMIT_42;
  ref->robot_status.launcher_id1_17_speed_limit = REF_LAUNCH_SPEED;
  ref->robot_status.launcher_42_speed_limit = REF_LAUNCH_SPEED;
  ref->robot_status.chassis_power_limit = REF_POWER_LIMIT;
  ref->power_heat.chassis_pwr_buff = REF_POWER_BUFF;
#endif

  return DEVICE_OK;
}

uint8_t referee_refresh_ui(referee_trans_t *ref) {
  ASSERT(ref);

#if UI_MODE_OP
  ui_ele_t ele;
  ui_string_t string;

  const float kW = ref->ui.screen->width;
  const float kH = ref->ui.screen->height;

  float box_pos_left = 0.0f, box_pos_right = 0.0f;

  static ui_graphic_op_t graphic_op = UI_GRAPHIC_OP_ADD;

  /* UI静态元素刷新 */
  if (xSemaphoreTake(ref->sem.ui_slow_refresh, 0)) {
    graphic_op = UI_GRAPHIC_OP_ADD;
    ref->ui.refresh_fsm = 0;

    ui_draw_string(&string, "8", graphic_op, UI_GRAPHIC_LAYER_CONST, UI_GREEN,
                   UI_DEFAULT_WIDTH * 10, 80, UI_CHAR_DEFAULT_WIDTH,
                   (uint16_t)(kW * REF_UI_RIGHT_START_W),
                   (uint16_t)(kH * REF_UI_MODE_LINE1_H),
                   "CHAS  FLLW  FL35  ROTR");
    ui_stash_string(&(ref->ui), &string);

    ui_draw_string(&string, "9", graphic_op, UI_GRAPHIC_LAYER_CONST, UI_GREEN,
                   UI_DEFAULT_WIDTH * 10, 80, UI_CHAR_DEFAULT_WIDTH,
                   (uint16_t)(kW * REF_UI_RIGHT_START_W),
                   (uint16_t)(kH * REF_UI_MODE_LINE2_H),
                   "GMBL  RELX  ABSL  RLTV");
    ui_stash_string(&(ref->ui), &string);

    ui_draw_string(&string, "a", graphic_op, UI_GRAPHIC_LAYER_CONST, UI_GREEN,
                   UI_DEFAULT_WIDTH * 10, 80, UI_CHAR_DEFAULT_WIDTH,
                   (uint16_t)(kW * REF_UI_RIGHT_START_W),
                   (uint16_t)(kH * REF_UI_MODE_LINE3_H),
                   "SHOT  RELX  SAFE  LOAD");
    ui_stash_string(&(ref->ui), &string);

    ui_draw_string(&string, "b", graphic_op, UI_GRAPHIC_LAYER_CONST, UI_GREEN,
                   UI_DEFAULT_WIDTH * 10, 80, UI_CHAR_DEFAULT_WIDTH,
                   (uint16_t)(kW * REF_UI_RIGHT_START_W),
                   (uint16_t)(kH * REF_UI_MODE_LINE4_H),
                   "FIRE  SNGL  BRST  CONT");
    ui_stash_string(&(ref->ui), &string);

    ui_draw_line(&ele, "c", graphic_op, UI_GRAPHIC_LAYER_CONST, UI_GREEN,
                 UI_DEFAULT_WIDTH * 3, (uint16_t)(kW * 0.4f),
                 (uint16_t)(kH * 0.2f), (uint16_t)(kW * 0.4f),
                 (uint16_t)(kH * 0.2f + 50.f));
    ui_stash_graphic(&(ref->ui), &ele);

    ui_draw_string(&string, "d", graphic_op, UI_GRAPHIC_LAYER_CONST, UI_GREEN,
                   UI_DEFAULT_WIDTH * 10, 80, UI_CHAR_DEFAULT_WIDTH,
                   (uint16_t)(kW * REF_UI_RIGHT_START_W), (uint16_t)(kH * 0.4f),
                   "CTRL  JS  KM");
    ui_stash_string(&(ref->ui), &string);

    ui_draw_string(&string, "e", graphic_op, UI_GRAPHIC_LAYER_CONST, UI_GREEN,
                   UI_DEFAULT_WIDTH * 20, 80, UI_CHAR_DEFAULT_WIDTH * 2,
                   (uint16_t)(kW * 0.6f - 26.0f), (uint16_t)(kH * 0.2f + 10.0f),
                   "CAP");
    ui_stash_string(&(ref->ui), &string);

    return DEVICE_OK;
  }

  /* UI动态元素刷新 */
  if (xSemaphoreTake(ref->sem.ui_fast_refresh, 0)) {
    /* 使用状态机算法，每次更新一个图层 */
    switch (ref->ui.refresh_fsm) {
      case 0: {
        ref->ui.refresh_fsm++;

        /* 更新电容状态 */
        if (ref->cap_ui.online) {
          ui_draw_arc(&ele, "3", graphic_op, UI_GRAPHIC_LAYER_CAP, UI_GREEN, 0,
                      (uint16_t)(ref->cap_ui.percentage * 360.f),
                      UI_DEFAULT_WIDTH * 5, (uint16_t)(kW * 0.6f),
                      (uint16_t)(kH * 0.2f), 50, 50);
        } else {
          ui_draw_arc(&ele, "3", graphic_op, UI_GRAPHIC_LAYER_CAP, UI_YELLOW, 0,
                      360, UI_DEFAULT_WIDTH * 5, (uint16_t)(kW * 0.6f),
                      (uint16_t)(kH * 0.2), 50, 50);
        }
        ui_stash_graphic(&(ref->ui), &ele);

        /* 更新云台模式选择框 */
        switch (ref->gimbal_ui.mode) {
          case GIMBAL_MODE_RELAX:
            box_pos_left = REF_UI_MODE_OFFSET_2_LEFT;
            box_pos_right = REF_UI_MODE_OFFSET_2_RIGHT;
            break;
          case GIMBAL_MODE_ABSOLUTE:
            box_pos_left = REF_UI_MODE_OFFSET_3_LEFT;
            box_pos_right = REF_UI_MODE_OFFSET_3_RIGHT;
            break;
          case GIMBAL_MODE_RELATIVE:
            box_pos_left = REF_UI_MODE_OFFSET_4_LEFT;
            box_pos_right = REF_UI_MODE_OFFSET_4_RIGHT;
            break;
          default:
            box_pos_left = 0.0f;
            box_pos_right = 0.0f;
            break;
        }
        if (box_pos_left != 0.0f && box_pos_right != 0.0f) {
          ui_draw_rectangle(
              &ele, "4", graphic_op, UI_GRAPHIC_LAYER_GIMBAL, UI_GREEN,
              UI_DEFAULT_WIDTH,
              (uint16_t)(kW * REF_UI_RIGHT_START_W + box_pos_left),
              (uint16_t)(kH * REF_UI_MODE_LINE2_H + REF_UI_BOX_UP_OFFSET),
              (uint16_t)(kW * REF_UI_RIGHT_START_W + box_pos_right),
              (uint16_t)(kH * REF_UI_MODE_LINE2_H + REF_UI_BOX_BOT_OFFSET));
          ui_stash_graphic(&(ref->ui), &ele);
        }

        /* 更新发射器模式选择框 */
        switch (ref->launcher_ui.mode) {
          case LAUNCHER_MODE_RELAX:
            box_pos_left = REF_UI_MODE_OFFSET_2_LEFT;
            box_pos_right = REF_UI_MODE_OFFSET_2_RIGHT;
            break;
          case LAUNCHER_MODE_SAFE:
            box_pos_left = REF_UI_MODE_OFFSET_3_LEFT;
            box_pos_right = REF_UI_MODE_OFFSET_3_RIGHT;
            break;
          case LAUNCHER_MODE_LOADED:
            box_pos_left = REF_UI_MODE_OFFSET_4_LEFT;
            box_pos_right = REF_UI_MODE_OFFSET_4_RIGHT;
            break;
          default:
            box_pos_left = 0.0f;
            box_pos_right = 0.0f;
            break;
        }
        if (box_pos_left != 0.0f && box_pos_right != 0.0f) {
          ui_draw_rectangle(
              &ele, "5", graphic_op, UI_GRAPHIC_LAYER_LAUNCHER, UI_GREEN,
              UI_DEFAULT_WIDTH,
              (uint16_t)(kW * REF_UI_RIGHT_START_W + box_pos_left),
              (uint16_t)(kH * REF_UI_MODE_LINE3_H + REF_UI_BOX_UP_OFFSET),
              (uint16_t)(kW * REF_UI_RIGHT_START_W + box_pos_right),
              (uint16_t)(kH * REF_UI_MODE_LINE3_H + REF_UI_BOX_BOT_OFFSET));
          ui_stash_graphic(&(ref->ui), &ele);
        }

        /* 更新开火模式选择框 */
        switch (ref->launcher_ui.fire) {
          case FIRE_MODE_SINGLE:
            box_pos_left = REF_UI_MODE_OFFSET_2_LEFT;
            box_pos_right = REF_UI_MODE_OFFSET_2_RIGHT;
            break;
          case FIRE_MODE_BURST:
            box_pos_left = REF_UI_MODE_OFFSET_3_LEFT;
            box_pos_right = REF_UI_MODE_OFFSET_3_RIGHT;
            break;
          case FIRE_MODE_CONT:
            box_pos_left = REF_UI_MODE_OFFSET_4_LEFT;
            box_pos_right = REF_UI_MODE_OFFSET_4_RIGHT;
            break;
          default:
            box_pos_left = 0.0f;
            box_pos_right = 0.0f;
            break;
        }
        if (box_pos_left != 0.0f && box_pos_right != 0.0f) {
          ui_draw_rectangle(
              &ele, "6", graphic_op, UI_GRAPHIC_LAYER_LAUNCHER, UI_GREEN,
              UI_DEFAULT_WIDTH,
              (uint16_t)(kW * REF_UI_RIGHT_START_W + box_pos_left),
              (uint16_t)(kH * REF_UI_MODE_LINE4_H + REF_UI_BOX_UP_OFFSET),
              (uint16_t)(kW * REF_UI_RIGHT_START_W + box_pos_right),
              (uint16_t)(kH * REF_UI_MODE_LINE4_H + REF_UI_BOX_BOT_OFFSET));
          ui_stash_graphic(&(ref->ui), &ele);
        }

        /* 更新控制权选择框 */
        switch (ref->cmd_ui.ctrl_method) {
          case CMD_METHOD_MOUSE_KEYBOARD:
            ui_draw_rectangle(&ele, "7", graphic_op, UI_GRAPHIC_LAYER_CMD,
                              UI_GREEN, UI_DEFAULT_WIDTH,
                              (uint16_t)(kW * REF_UI_RIGHT_START_W + 96.f),
                              (uint16_t)(kH * 0.4f + REF_UI_BOX_UP_OFFSET),
                              (uint16_t)(kW * REF_UI_RIGHT_START_W + 120.f),
                              (uint16_t)(kH * 0.4f + REF_UI_BOX_BOT_OFFSET));
            break;
          case CMD_METHOD_JOYSTICK_SWITCH:
            ui_draw_rectangle(&ele, "7", graphic_op, UI_GRAPHIC_LAYER_CMD,
                              UI_GREEN, UI_DEFAULT_WIDTH,
                              (uint16_t)(kW * REF_UI_RIGHT_START_W + 56.f),
                              (uint16_t)(kH * 0.4f + REF_UI_BOX_UP_OFFSET),
                              (uint16_t)(kW * REF_UI_RIGHT_START_W + 80.f),
                              (uint16_t)(kH * 0.4f + REF_UI_BOX_BOT_OFFSET));
            break;
        }
        ui_stash_graphic(&(ref->ui), &ele);
        break;
      }
      case 1: {
        ref->ui.refresh_fsm++;

        /*更新拨弹电机状态*/
        float trig_start = ref->launcher_ui.trig / M_2PI * 360.f;
        float trig_end = ref->launcher_ui.trig / M_2PI * 360.f;
        circle_add(&trig_end, 60.0f, 360);
        if (trig_end >= 360.f) trig_end = 360.f;
        ui_draw_arc(&ele, "f", graphic_op, UI_GRAPHIC_LAYER_LAUNCHER, UI_GREEN,
                    (uint16_t)trig_start, (uint16_t)trig_end,
                    UI_DEFAULT_WIDTH * 5, (uint16_t)(kW * 0.4f),
                    (uint16_t)(kH * 0.1f), 50, 50);
        ui_stash_graphic(&(ref->ui), &ele);

        /*更新摩擦轮电机状态*/
        if (ref->launcher_ui.fric_percent[0] == 0 ||
            ref->launcher_ui.fric_percent[1] == 0) {
          ui_draw_arc(&ele, "g", graphic_op, UI_GRAPHIC_LAYER_LAUNCHER,
                      UI_YELLOW, 0, 360, UI_DEFAULT_WIDTH * 5,
                      (uint16_t)(kW * 0.6f), (uint16_t)(kH * 0.1f), 50, 50);
        } else {
          ui_draw_arc(&ele, "g", graphic_op, UI_GRAPHIC_LAYER_LAUNCHER,
                      UI_GREEN,
                      (uint16_t)180 - 170 * ref->launcher_ui.fric_percent[0],
                      (uint16_t)(180 + 170 * ref->launcher_ui.fric_percent[1]),
                      UI_DEFAULT_WIDTH * 5, (uint16_t)(kW * 0.6f),
                      (uint16_t)(kH * 0.1f), 50, 50);
        }
        ui_stash_graphic(&(ref->ui), &ele);

        break;
      }

      case 2: {
        ref->ui.refresh_fsm++;
        /* 更新云台底盘相对方位 */
        const float kLEN = 22;
        ui_draw_line(
            &ele, "1", graphic_op, UI_GRAPHIC_LAYER_CHASSIS, UI_GREEN,
            UI_DEFAULT_WIDTH * 12, (uint16_t)(kW * 0.4f), (uint16_t)(kH * 0.2f),
            (uint16_t)(kW * 0.4f + sinf(ref->chassis_ui.angle) * 2 * kLEN),
            (uint16_t)(kH * 0.2f + cosf(ref->chassis_ui.angle) * 2 * kLEN));

        ui_stash_graphic(&(ref->ui), &ele);

        /* 更新底盘模式选择框 */
        switch (ref->chassis_ui.mode) {
          case CHASSIS_MODE_FOLLOW_GIMBAL:
            box_pos_left = REF_UI_MODE_OFFSET_2_LEFT;
            box_pos_right = REF_UI_MODE_OFFSET_2_RIGHT;
            break;
          case CHASSIS_MODE_FOLLOW_GIMBAL_35:
            box_pos_left = REF_UI_MODE_OFFSET_3_LEFT;
            box_pos_right = REF_UI_MODE_OFFSET_3_RIGHT;
            break;
          case CHASSIS_MODE_ROTOR:
            box_pos_left = REF_UI_MODE_OFFSET_4_LEFT;
            box_pos_right = REF_UI_MODE_OFFSET_4_RIGHT;
            break;
          default:
            box_pos_left = 0.0f;
            box_pos_right = 0.0f;
            break;
        }
        if (box_pos_left != 0.0f && box_pos_right != 0.0f) {
          ui_draw_rectangle(
              &ele, "2", graphic_op, UI_GRAPHIC_LAYER_CHASSIS, UI_GREEN,
              UI_DEFAULT_WIDTH,
              (uint16_t)(kW * REF_UI_RIGHT_START_W + box_pos_left),
              (uint16_t)(kH * REF_UI_MODE_LINE1_H + REF_UI_BOX_UP_OFFSET),
              (uint16_t)(kW * REF_UI_RIGHT_START_W + box_pos_right),
              (uint16_t)(kH * REF_UI_MODE_LINE1_H + REF_UI_BOX_BOT_OFFSET));

          ui_stash_graphic(&(ref->ui), &ele);
        }
        break;
      }

      default:
        ref->ui.refresh_fsm = 0;
    }
  }

  if (graphic_op == UI_GRAPHIC_OP_ADD && ref->ui.refresh_fsm == 1)
    graphic_op = UI_GRAPHIC_OP_REWRITE;

#elif UI_MODE_REMOTE
  if (xSemaphoreTake(ref->sem.ui_fast_refresh, 0)) {
    // TODO:
  }

  if (xSemaphoreTake(ref->sem.ui_slow_refresh, 0)) {
    // TODO:
  }

#endif
  return DEVICE_OK;
}
/**
 * @brief 组装UI包
 *
 * @param ui UI数据
 * @param ref 裁判系统数据
 * @return int8_t 0代表成功
 */
int8_t referee_pack_ui_packet(referee_trans_t *ref) {
  ui_ele_t *ele = NULL;
  ui_string_t string;
  ui_del_t del;

  referee_student_cmd_id_t ui_cmd_id;
  static const size_t ksize_data_header =
      sizeof(referee_inter_student_header_t);
  size_t size_data_content;
  static const size_t ksize_packet_crc = sizeof(uint16_t);
  void *source = NULL;

  if (!ui_pop_del(&(ref->ui), &del)) {
    source = &del;
    size_data_content = sizeof(ui_del_t);
    ui_cmd_id = REF_STDNT_CMD_ID_UI_DEL;
  } else if (ref->ui.stack.size.graphic) { /* 绘制图形 */
    if (ref->ui.stack.size.graphic <= 1) {
      size_data_content = sizeof(ui_ele_t) * 1;
      ui_cmd_id = REF_STDNT_CMD_ID_UI_DRAW1;

    } else if (ref->ui.stack.size.graphic <= 2) {
      size_data_content = sizeof(ui_ele_t) * 2;
      ui_cmd_id = REF_STDNT_CMD_ID_UI_DRAW2;

    } else if (ref->ui.stack.size.graphic <= 5) {
      size_data_content = sizeof(ui_ele_t) * 5;
      ui_cmd_id = REF_STDNT_CMD_ID_UI_DRAW5;

    } else if (ref->ui.stack.size.graphic <= 7) {
      size_data_content = sizeof(ui_ele_t) * 7;
      ui_cmd_id = REF_STDNT_CMD_ID_UI_DRAW7;

    } else {
      return DEVICE_ERR;
    }
    ele = pvPortMalloc(size_data_content);
    ui_ele_t *cursor = ele;
    while (!ui_pop_graphic(&(ref->ui), cursor)) {
      cursor++;
    }
    source = ele;
  } else if (!ui_pop_string(&(ref->ui), &string)) { /* 绘制字符 */
    source = &string;
    size_data_content = sizeof(ui_string_t);
    ui_cmd_id = REF_STDNT_CMD_ID_UI_STR;
  } else {
    return DEVICE_ERR;
  }

  ref->packet.size =
      sizeof(referee_ui_packet_head_t) + size_data_content + ksize_packet_crc;

  ref->packet.data = pvPortMalloc(ref->packet.size);

  referee_ui_packet_head_t *packet_head =
      (referee_ui_packet_head_t *)(ref->packet.data);

  referee_set_packet_header(&(packet_head->header),
                            ksize_data_header + (uint16_t)size_data_content);
  packet_head->cmd_id = REF_CMD_ID_INTER_STUDENT;
  referee_set_ui_header(&(packet_head->student_header), ui_cmd_id,
                        gref_recv->robot_status.robot_id);
  memcpy(ref->packet.data + sizeof(referee_ui_packet_head_t), source,
         size_data_content);

  vPortFree(ele);
  uint16_t *crc =
      (uint16_t *)(ref->packet.data + ref->packet.size - ksize_packet_crc);
  *crc = crc16_calc((const uint8_t *)ref->packet.data,
                    ref->packet.size - ksize_packet_crc, CRC16_INIT);

  return DEVICE_OK;
}

int8_t referee_start_transmit(referee_trans_t *ref) {
  if (ref->packet.data != NULL && ref->packet.size > 0) {
    memcpy(txbuf, ref->packet.data, ref->packet.size);
    vPortFree(ref->packet.data);
    ref->packet.data = NULL;
  } else {
    xSemaphoreGive(ref->sem.packet_sent);
    return DEVICE_ERR;
  }

  if (bsp_uart_transmit(BSP_UART_REF, txbuf, (uint16_t)ref->packet.size,
                        false) == HAL_OK) {
    return DEVICE_OK;
  } else {
    xSemaphoreGive(ref->sem.packet_sent);
    return DEVICE_ERR;
  }
}

bool referee_wait_trans_cplt(referee_trans_t *ref, uint32_t timeout) {
  return xSemaphoreTake(ref->sem.packet_sent, pdMS_TO_TICKS(timeout)) == pdTRUE;
}

uint8_t referee_pack_for_chassis(referee_for_chassis_t *c_ref,
                                 const referee_recv_t *ref) {
  c_ref->chassis_power_limit = ref->robot_status.chassis_power_limit;
  c_ref->chassis_pwr_buff = ref->power_heat.chassis_pwr_buff;
  c_ref->chassis_watt = ref->power_heat.chassis_watt;
  c_ref->status = ref->status;
  return DEVICE_OK;
}

uint8_t referee_pack_for_launcher(referee_for_launcher_t *l_ref,
                                  const referee_recv_t *ref) {
  memcpy(&(l_ref->power_heat), &(ref->power_heat), sizeof(l_ref->power_heat));
  memcpy(&(l_ref->robot_status), &(ref->robot_status),
         sizeof(l_ref->robot_status));
  memcpy(&(l_ref->launcher_data), &(ref->launcher_data),
         sizeof(l_ref->launcher_data));
  l_ref->status = ref->status;
  return DEVICE_OK;
}

uint8_t referee_pack_for_ai(referee_for_ai_t *ai_ref,
                            const referee_recv_t *ref) {
  memset(ai_ref, 0, sizeof(*ai_ref));

#if ID_HERO
  ai_ref->ball_speed = ref->robot_status.launcher_42_speed_limit;
#else
  ai_ref->ball_speed = ref->robot_status.launcher_id1_17_speed_limit;
#endif

  ai_ref->max_hp = ref->robot_status.max_hp;

  ai_ref->hp = ref->robot_status.remain_hp;

  if (ref->robot_status.robot_id < REF_BOT_BLU_HERO)
    ai_ref->team = AI_TEAM_RED;
  else
    ai_ref->team = AI_TEAM_BLUE;

  ai_ref->status = ref->status;

  if (ref->rfid.high_ground == 1)
    ai_ref->robot_buff |= AI_RFID_SNIP;

  else if (ref->rfid.energy_mech == 1)
    ai_ref->robot_buff |= AI_RFID_BUFF;

  else
    ai_ref->robot_buff = 0;

  switch (ref->game_status.game_type) {
    case REF_GAME_TYPE_RMUC:
      ai_ref->game_type = AI_RACE_RMUC;
      break;
    case REF_GAME_TYPE_RMUT:
      ai_ref->game_type = AI_RACE_RMUT;
      break;
    case REF_GAME_TYPE_RMUL_3V3:
      ai_ref->game_type = AI_RACE_RMUL3;
      break;
    case REF_GAME_TYPE_RMUL_1V1:
      ai_ref->game_type = AI_RACE_RMUL1;
      break;
    default:
      return DEVICE_ERR;
  }

  switch (ref->robot_status.robot_id % 100) {
    case REF_BOT_RED_HERO:
      ai_ref->robot_id = AI_ARM_HERO;
      break;
    case REF_BOT_RED_ENGINEER:
      ai_ref->robot_id = AI_ARM_ENGINEER;
      break;
    case REF_BOT_RED_DRONE:
      ai_ref->robot_id = AI_ARM_DRONE;
      break;
    case REF_BOT_RED_SENTRY:
      ai_ref->robot_id = AI_ARM_SENTRY;
      break;
    case REF_BOT_RED_RADER:
      ai_ref->robot_id = AI_ARM_RADAR;
      break;
    default:
      ai_ref->robot_id = AI_ARM_INFANTRY;
  }
  return DEVICE_OK;
}
