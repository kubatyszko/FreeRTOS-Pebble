/* bluetooth.c
 * routines for controlling a bluetooth stack
 * RebbleOS
 *
 * Author: Barry Carter <barry.carter@gmail.com>
 */

#include "FreeRTOS.h"
#include "task.h" /* xTaskCreate */
#include "queue.h" /* xQueueCreate */
#include "platform.h" /* hw_backlight_set */
#include "log.h" /* KERN_LOG */
#include "backlight.h"
#include "ambient.h"
#include "rebble_memory.h"
#include "rebbleos.h"
#include "bluetooth.h"
#include "ringbuf.h"

static TaskHandle_t _bt_task;
static StackType_t _bt_task_stack[4000];
static StaticTask_t _bt_task_buf;
static xQueueHandle _bt_cmd_queue;
static SemaphoreHandle_t _bt_tx_mutex;
static StaticSemaphore_t _bt_tx_mutex_buf;

static void _bt_thread(void *pvParameters);

// ring buffer (statically allocated)
// ringbuf_t rx_buf
#define TX_RING_BUF_SIZE 128
#define RX_RING_BUF_SIZE 128
// statically allocate the ring buffer
// uint8_t tx_ring_buf_buffer[TX_RING_BUF_SIZE];
ringbuf_t tx_ring_buf;
ringbuf_t rx_ring_buf;

void bluetooth_init(void)
{
    _bt_task = xTaskCreateStatic(_bt_thread, "BT", 4000, NULL, tskIDLE_PRIORITY + 3UL, _bt_task_stack, &_bt_task_buf);
    
    _bt_tx_mutex = xSemaphoreCreateMutexStatic(&_bt_tx_mutex_buf);
    _bt_cmd_queue = xQueueCreate(1, sizeof(uint8_t));
    
    // statically allocate the tx buffer
//     tx_ring_buf.buf = tx_ring_buf_buffer;
//     tx_ring_buf.size = TX_RING_BUF_SIZE + 1; // one byte for full confition (see ringbuf.c)
    tx_ring_buf = ringbuf_new(TX_RING_BUF_SIZE);
    rx_ring_buf = ringbuf_new(RX_RING_BUF_SIZE);
    
    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "Bluetooth Tasks Created");
}

/*
 * Just send some raw data
 * returns bytes sent
 */
uint32_t bluetooth_send_serial_raw(uint8_t *data, size_t len)
{
    if (!rebbleos_module_is_enabled(MODULE_BLUETOOTH)) return 0;
//     SYS_LOG("BT", APP_LOG_LEVEL_INFO, "Sending Data");
    // can't guarantee these test logs dont come from an ISR
//     xSemaphoreTake(_bt_tx_mutex, portMAX_DELAY);
    // fill up the buffer
    // XXX TODO block when full? At least check!
    ringbuf_memcpy_into(tx_ring_buf, data, len);
    btstack_request_tx();
    
//     xSemaphoreGive(_bt_tx_mutex);
    
    return len;
}

uint32_t bluetooth_tx_buf_get_bytes(uint8_t *data, size_t len)
{
    size_t count = ringbuf_bytes_used(tx_ring_buf);
    if (len > count)
        len = count;
    ringbuf_memcpy_from(data, tx_ring_buf, count);
    return count;
}
static char gotcha[] = "Gotcha";
/*
 * Some data arrived from the stack
 */
void bluetooth_data_rx(uint8_t *data, size_t len)
{
    // read the data out into our ring buffer
    ringbuf_memcpy_into(rx_ring_buf, data, len);
    
    bluetooth_send_serial_raw(gotcha, sizeof(gotcha));
    // check for a a packet
    
    // and stuff
}

typedef struct bt_packet_tx_t
{
    uint8_t *data;
    size_t len;
} bt_packet_tx;

bt_packet_tx packet;

void bluetooth_send(uint8_t *data, size_t len)
{
    packet.data = data;
    packet.len = len;
    xQueueSendToBack(_bt_cmd_queue, &packet, 0);
}

/*
 * Bluetooth thread. The device is initialised here
 * 
 * XXX move freertos runloop code to here
 */
static void _bt_thread(void *pvParameters)
{  
    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "Starting Bluetooth Module");
    // Get bluetooth running
    hw_bluetooth_init();
    
    // all beng well, we are blocked above.
    // All not being well, we arrive here.
    
    // Error!
    SYS_LOG("BT", APP_LOG_LEVEL_ERROR, "Bluetooth Module DISABLED");
    rebbleos_module_set_status(MODULE_BLUETOOTH, MODULE_DISABLED, MODULE_ERROR);
    vTaskDelete(_bt_task);
    return;
}

// TODO
static void _bt_cmd_thread(void *pvParameters)
{
    bt_packet_tx pkt;
    for( ;; )
    {
        // Sit and wait for a wakeup. We will wake when we have a packet to send
        if (xQueueReceive(_bt_cmd_queue, &pkt, portMAX_DELAY))
        {
            bluetooth_send_serial_raw(pkt.data, pkt.len);
        }
        else
        {
            // nothing emerged from the buffer
        }
    }
}
