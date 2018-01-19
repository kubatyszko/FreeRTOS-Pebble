#pragma once

void bluetooth_data_rx_notify(size_t len);
uint32_t bluetooth_send_serial_raw(uint8_t *data, size_t len);
uint32_t bluetooth_tx_buf_get_bytes(uint8_t *data, size_t len);
void bluetooth_data_rx(uint8_t *data, size_t len);
