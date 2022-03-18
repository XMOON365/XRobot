#include "bsp_can.h"

#include <string.h>

#include "comp_utils.h"

static can_group_t can_groups[BSP_CAN_NUM];

static bsp_callback_t callback_list[BSP_CAN_NUM][BSP_CAN_CB_NUM];

static bsp_can_t can_get(CAN_HandleTypeDef *hcan) {
  if (hcan->Instance == CAN2)
    return BSP_CAN_2;
  else if (hcan->Instance == CAN1)
    return BSP_CAN_1;
  else
    return BSP_CAN_ERR;
}

static void bsp_can_callback(bsp_can_callback_t cb_type,
                             CAN_HandleTypeDef *hcan) {
  bsp_can_t bsp_can = can_get(hcan);
  if (bsp_can != BSP_CAN_ERR) {
    bsp_callback_t cb = callback_list[bsp_can][cb_type];

    if (cb.fn) {
      cb.fn(cb.arg);
    }
  }
}

void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef *hcan) {
  bsp_can_callback(HAL_CAN_TX_MAILBOX0_CPLT_CB, hcan);
}

void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef *hcan) {
  bsp_can_callback(HAL_CAN_TX_MAILBOX1_CPLT_CB, hcan);
}

void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef *hcan) {
  bsp_can_callback(HAL_CAN_TX_MAILBOX2_CPLT_CB, hcan);
}

void HAL_CAN_TxMailbox0AbortCallback(CAN_HandleTypeDef *hcan) {
  bsp_can_callback(HAL_CAN_TX_MAILBOX0_ABORT_CB, hcan);
}

void HAL_CAN_TxMailbox1AbortCallback(CAN_HandleTypeDef *hcan) {
  bsp_can_callback(HAL_CAN_TX_MAILBOX1_ABORT_CB, hcan);
}

void HAL_CAN_TxMailbox2AbortCallback(CAN_HandleTypeDef *hcan) {
  bsp_can_callback(HAL_CAN_TX_MAILBOX2_ABORT_CB, hcan);
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan) {
  bsp_can_callback(HAL_CAN_RX_FIFO0_MSG_PENDING_CB, hcan);
}

void HAL_CAN_RxFifo0FullCallback(CAN_HandleTypeDef *hcan) {
  bsp_can_callback(HAL_CAN_RX_FIFO0_FULL_CB, hcan);
}

void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef *hcan) {
  bsp_can_callback(HAL_CAN_RX_FIFO1_MSG_PENDING_CB, hcan);
}

void HAL_CAN_RxFifo1FullCallback(CAN_HandleTypeDef *hcan) {
  bsp_can_callback(HAL_CAN_RX_FIFO1_FULL_CB, hcan);
}

void HAL_CAN_SleepCallback(CAN_HandleTypeDef *hcan) {
  bsp_can_callback(HAL_CAN_SLEEP_CB, hcan);
}

void HAL_CAN_WakeUpFromRxMsgCallback(CAN_HandleTypeDef *hcan) {
  bsp_can_callback(HAL_CAN_WAKEUP_FROM_RX_MSG_CB, hcan);
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan) {
  bsp_can_callback(HAL_CAN_ERROR_CB, hcan);
}

CAN_HandleTypeDef *bsp_can_get_handle(bsp_can_t can) {
  switch (can) {
    case BSP_CAN_2:
      return &hcan2;
    case BSP_CAN_1:
      return &hcan1;
    default:
      return NULL;
  }
}

int8_t bsp_can_register_callback(bsp_can_t can, bsp_can_callback_t type,
                                 void (*callback)(void *), void *callback_arg) {
  ASSERT(callback);
  ASSERT(type != BSP_CAN_CB_NUM);

  callback_list[can][type].fn = callback;
  callback_list[can][type].arg = callback_arg;
  return BSP_OK;
}

int8_t bsp_can_register_subscriber(bsp_can_t can, uint32_t index,
                                   uint32_t number,
                                   void (*cb)(can_rx_item_t *, void *),
                                   void *callback_arg) {
  ASSERT(cb);

  if (can_groups[can].suber_number >= CAN_MAX_SUBER_NUMBER) return BSP_ERR;

  can_groups[can].suber[can_groups[can].suber_number].cb = cb;
  can_groups[can].suber[can_groups[can].suber_number].index = index;
  can_groups[can].suber[can_groups[can].suber_number].number = number;
  can_groups[can].suber[can_groups[can].suber_number].callback_arg =
      callback_arg;
  can_groups[can].suber_number++;

  return BSP_OK;
}

int8_t bsp_can_publish_data(bsp_can_t can, uint32_t StdId, uint8_t *data) {
  for (int i = 0; i < can_groups[can].suber_number; i++) {
    uint32_t index = StdId - can_groups[can].suber[i].index;
    if (index < can_groups[can].suber[i].number) {
      can_rx_item_t rx;
      rx.index = index;
      memcpy(rx.data, data, sizeof(rx.data));
      can_groups[can].suber[i].cb(&rx, can_groups[can].suber[i].callback_arg);
      return BSP_OK;
    }
  }
  return BSP_ERR;
}

int8_t can_trans_packet(bsp_can_t can, uint32_t StdId, uint8_t *data,
                        uint32_t *mailbox) {
  CAN_TxHeaderTypeDef header;
  header.StdId = StdId;
  header.IDE = CAN_ID_STD;
  header.RTR = CAN_RTR_DATA;
  header.TransmitGlobalTime = DISABLE;
  header.DLC = 8;

  if (HAL_CAN_AddTxMessage(bsp_can_get_handle(can), &header, data, mailbox) ==
      HAL_OK)
    return BSP_OK;
  else
    return BSP_ERR;
}