/* bluetooth.c
 * routines for controlling a bluetooth stack
 * RebbleOS
 *
 * Author: Barry Carter <barry.carter@gmail.com>
 */
/*
 * General flow:
 * RX
 * 
 * 
 * TX
 * A packet is genreated and posted to the cmd thread
 * btstack_rebble takes a ref to this data,
 * and waits for a ready to send message fromt he bt stack.
 *  * NOTE the memory is not copied, it is sent with supplied buf
 * 
 * RX
 * An incoming packet is colelcted in btstack_rebble
 * The data is copied into our rx ring buffer (in here)
 * once we have some data, we look over the buffer and check for packets
 * If we have packets, we recombine them and process as required.
 * 
 * We use the ringbug as I have seen packets span multiple tranactions
 * TODO: See if we can determine the real MTU and if this goes away. 
 *  could drop ringbuf
 * 
 * NOTES:
 * we have locking and whatnot. Test it.
 * clocks are a bit rough. check.
 * memory. stack sizes are large. reduce.
 * determine max MTU size (eats ram)
 * tune rx buffer size
 */

#include "FreeRTOS.h"
#include "task.h" /* xTaskCreate */
#include "timers.h" /* xTimerCreate */
#include "queue.h" /* xQueueCreate */
#include "platform.h" /* hw_backlight_set */
#include "log.h" /* KERN_LOG */
#include "backlight.h"
#include "ambient.h"
#include "rebble_memory.h"
#include "rebbleos.h"
#include "rbl_bluetooth.h"
#include "ringbuf.h"
#include "pebble_protocol.h"

// BT runloop
static TaskHandle_t _bt_task;
static StackType_t _bt_task_stack[2000];
static StaticTask_t _bt_task_buf;

// for the command processer
static TaskHandle_t _bt_cmd_task;
static StackType_t _bt_cmd_task_stack[1500];
static StaticTask_t _bt_cmd_task_buf;

static xQueueHandle _bt_cmd_queue;
static SemaphoreHandle_t _bt_tx_mutex;
static StaticSemaphore_t _bt_tx_mutex_buf;

static void _bt_thread(void *pvParameters);
static void _bt_cmd_thread(void *pvParameters);


#define BUFFER_CLEAR_TIMER_MS 5
TimerHandle_t _timer_buffer_refresh;
StaticTimer_t _timer_buffer_refresh_buf;
void _timer_callback(TimerHandle_t timer);

typedef struct pbl_transport_packet_t {
    uint16_t length;
    uint16_t endpoint;
    uint8_t *data;
} __attribute__((__packed__)) pbl_transport_packet;

#define PACKET_TYPE_RX 0
#define PACKET_TYPE_TX 1

typedef struct bt_packet_tx_t {
    uint8_t pkt_type;
    uint16_t endpoint;
    uint8_t *data;
    size_t len;
} bt_packet_tx;

bt_packet_tx packet;


#define RX_RING_BUF_SIZE 128
ringbuf_t rx_ring_buf;

pbl_transport_packet *_parse_packet(void);

void bluetooth_init(void)
{
    _bt_task = xTaskCreateStatic(_bt_thread, "BT", 2000, NULL, tskIDLE_PRIORITY + 3UL, _bt_task_stack, &_bt_task_buf);
    _bt_cmd_task = xTaskCreateStatic(_bt_cmd_thread, "BTCmd", 1500, NULL, tskIDLE_PRIORITY + 4UL, _bt_cmd_task_stack, &_bt_cmd_task_buf);
    _timer_buffer_refresh = xTimerCreateStatic("T", pdMS_TO_TICKS(BUFFER_CLEAR_TIMER_MS), pdFALSE, ( void * ) 0, _timer_callback, &_timer_buffer_refresh_buf);
    
    _bt_tx_mutex = xSemaphoreCreateMutexStatic(&_bt_tx_mutex_buf);
    _bt_cmd_queue = xQueueCreate(1, sizeof(pbl_transport_packet *));
    
    rx_ring_buf = ringbuf_new(RX_RING_BUF_SIZE);
    ringbuf_reset(rx_ring_buf);

    
    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "Bluetooth Tasks Created");
}

/*
 * Just send some raw data
 * returns bytes sent
 * DO NOT CALL FROM ISR
 */
uint32_t bluetooth_send_serial_raw(uint8_t *data, size_t len)
{
    if (!rebbleos_module_is_enabled(MODULE_BLUETOOTH)) return 0;
//     SYS_LOG("BT", APP_LOG_LEVEL_INFO, "Sending Data");
    xSemaphoreTake(_bt_tx_mutex, portMAX_DELAY);

    bt_device_request_tx(data, len);
    
    // block this thread until we are done
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200)))
    {
        // clean unlock
        SYS_LOG("BT", APP_LOG_LEVEL_DEBUG, "Sent %d bytes", len);
    }
    else
    {
        // timed out
        SYS_LOG("BT", APP_LOG_LEVEL_ERROR, "Timed out sending!");
    }
    
    xSemaphoreGive(_bt_tx_mutex);
    
    return len;
}


/*
 * Some data arrived from the stack
 */
void bluetooth_data_rx(uint8_t *data, size_t len)
{
    // btstack is atomic
    xTimerStop(_timer_buffer_refresh, 0);
    
    ringbuf_memcpy_into(rx_ring_buf, data, len);
    SYS_LOG("BT", APP_LOG_LEVEL_ERROR, "RX: B 0x%x", *(data));
        
    // Notify the bluetooth threads
    bluetooth_data_rx_notify(len);
}

#define SWAP_UINT16(x) (((x) >> 8) | ((x) << 8))
#define SWAP_UINT32(x) (((x) >> 24) | (((x) & 0x00FF0000) >> 8) | (((x) & 0x0000FF00) << 8) | ((x) << 24))

/*
 * We are a simple packet parser. We are stupid
 * All incoming RX data goes into a circular buffer
 * after every incoming packet, we run a pass over the buffer
 * If we find a packet, we strip it out of the buffer
 * 
 * NOTE: we are also going to kill anythign in the buffer before
 * this packet. In theory it shouldn't matter. It should have been
 * processed, or it's junk
 * 
 */
pbl_transport_packet *_parse_packet(void)
{
    uint8_t *buf_start = ringbuf_tail(rx_ring_buf);
    size_t qlen = ringbuf_bytes_used(rx_ring_buf);
   
    pbl_transport_packet *pkt = buf_start;
    pkt->length = SWAP_UINT16(pkt->length);
    pkt->endpoint = SWAP_UINT16(pkt->endpoint);
    SYS_LOG("XXXXX", APP_LOG_LEVEL_INFO, "PKT L:0x%x E:0x%x", pkt->length, pkt->endpoint);
    /*
    for (int i = 0; i < qlen; i++) {
        SYS_LOG("XXXXX", APP_LOG_LEVEL_INFO, "0x%x", buf_start[i]);
    }*/
    
    if (qlen + 4 < pkt->length)
    {
        SYS_LOG("BT", APP_LOG_LEVEL_DEBUG, "RX: Data still coming %d %d", pkt->length, qlen);
        if(xTimerStart(_timer_buffer_refresh, 0) != pdPASS )
        {
            SYS_LOG("BT", APP_LOG_LEVEL_ERROR, "RX: Buffer Reset time won't start");
        }
        return NULL;
    }

    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "RX: GOOD packet. len %d end %d", pkt->length, pkt->endpoint);
    
    uint8_t *tmpdata = NULL;
    
    // random number
    if (pkt->length > 2048)
    {
        SYS_LOG("BT", APP_LOG_LEVEL_ERROR, "RX: payload length %d. Seems suspect!", pkt->length);
    }
    
    if (pkt->length > 0)
    {
        // copy into a new packet
        tmpdata = malloc(pkt->length);
        memcpy(tmpdata, buf_start + 4, pkt->length);
    }
    else
    {
        SYS_LOG("BT", APP_LOG_LEVEL_WARNING, "RX: payload length 0. Seems suspect!");
    }

    // copy into a new packet
    pbl_transport_packet *newpkt = malloc(sizeof(pbl_transport_packet));

    // destructively dequeue the whole packet
    ringbuf_memcpy_from((void *)newpkt, rx_ring_buf, pkt->length + 4);
    newpkt->data = tmpdata;
    
    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "RX: Done");
       
    return newpkt;
}

/* 
 * Request a TX through the outboud thread mechanism
 */
void bluetooth_send(uint8_t *data, size_t len)
{
    packet.pkt_type = PACKET_TYPE_TX;
    packet.data = data;
    packet.len = len;
    xQueueSendToBack(_bt_cmd_queue, &packet, 0);
}

/*
 * Send a Pebble packet right now
 */
void bluetooth_send_packet(uint16_t endpoint, uint8_t *data, uint16_t len)
{
    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "BT Got a Packet L:%d, E:%d", len, endpoint);

    uint8_t packet[len + 4];
    uint8_t *pxpacket = packet;
    *((uint16_t *)pxpacket) = SWAP_UINT16(len);
    *((uint16_t *)(pxpacket + 2)) = SWAP_UINT16(endpoint);
    memcpy(pxpacket + 4, data, len);
    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "BT Send packet L:%d, E:%d pD:%d", *((uint16_t *)pxpacket), *((uint16_t *)(pxpacket + 2)), data);
    // this will block until complete
    bluetooth_send_serial_raw(pxpacket, len + 4);
    
}

/* 
 * Some data arrived from the bluetooth device. We just want to queue the processing
 */
void bluetooth_data_rx_notify(size_t len)
{
    // XXX TODO LOCK ME REALLY URGENTLY Yo.
    packet.pkt_type = PACKET_TYPE_RX;
    packet.len = len;
    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "RX: Q PACKET");
    void *px_packet = &packet;
    xQueueSendToBack(_bt_cmd_queue, (void *)&px_packet, 0);
}

/*
 * We sent a packet. it was sent.
 */
void bluetooth_tx_complete_from_isr(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Notify the task that the transmission is complete.
    vTaskNotifyGiveFromISR(_bt_cmd_task, &xHigherPriorityTaskWoken);

    /* If xHigherPriorityTaskWoken is now set to pdTRUE then a context switch
    should be performed to ensure the interrupt returns directly to the highest
    priority task.  The macro used for this purpose is dependent on the port in
    use and may be called portEND_SWITCHING_ISR(). */
    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}

/*
 * Bluetooth thread. The device is initialised here
 * 
 * XXX move freertos runloop code to here?
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
    bt_packet_tx *pkt = NULL;
    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "BT CMD Thread started");
    
    uint8_t noty_data[] = {// test nofy data ripped from gb
        0x0, 0x49, 0xb, 0xc2, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0xce, 0x92, 0x83, 0xc4, 0x0, 0x0, 0x0, 0x0, 0x1e, 0xc8, 0x60, 0x5a, 0x1, 0x3, 0x1, 0x1, 0x4, 0x0, 0x54, 0x65, 0x73, 0x74, 0x2, 0x4, 0x0, 0x54, 0x65, 0x73, 0x74, 0x3, 0x4, 0x0, 0x54, 0x65, 0x73, 0x74, 0x3, 0x4, 0x1, 0x1, 0xb, 0x0, 0x44, 0x69, 0x73, 0x6d, 0x69, 0x73, 0x73, 0x20, 0x61, 0x6c, 0x6c, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
        
    bt_packet_tx tpkt = { 
        PACKET_TYPE_RX,
        3010, // endpoint
        noty_data,
        73        
    };
    void *px_packet = &tpkt;
    ringbuf_memcpy_into(rx_ring_buf, noty_data, 77);
    xQueueSendToBack(_bt_cmd_queue, (void *)&px_packet, 0);
    
    
    for( ;; )
    {
        // Si and wait for a wakeup. We will wake when we have a packet to send/recv
        if (xQueueReceive(_bt_cmd_queue, &pkt, portMAX_DELAY))
        {
            if (pkt->pkt_type == PACKET_TYPE_TX)
            {
                bluetooth_send_serial_raw(pkt->data, pkt->len);
            }
            else if (pkt->pkt_type == PACKET_TYPE_RX)
            {
                // some data arrived. We are responsible for the memory
                SYS_LOG("BT", APP_LOG_LEVEL_INFO, "BT Got Data L:%d", pkt->len);
                
                // check for a a packet
                pbl_transport_packet *bufpkt = _parse_packet();
                
                // Maybe the packet isn't ready. bail
                if (!bufpkt)
                {
                    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "BT Skip processing packet");
                    continue;
                }
                
                SYS_LOG("BT", APP_LOG_LEVEL_INFO, "BT Got a Packet L:%d, E:%d", bufpkt->length, bufpkt->endpoint);
                
                // Endpoint Firmware Version
                if(bufpkt->endpoint == ENDPOINT_FIRMWARE_VERSION)
                {       
                    process_version_packet(bufpkt->data);
                }
                // Endpoint Set Time
                else if (bufpkt->endpoint == ENDPOINT_SET_TIME)
                {
                    process_set_time_packet(bufpkt->data);
                }
                else if (bufpkt->endpoint == ENDPOINT_PHONE_MSG)
                {
                    process_notification_packet(bufpkt->data);
                }
                else
                {
                    // Complain
                    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "XXX Unimplemented Endpoint %d", bufpkt->endpoint);
                }
                
                free(bufpkt->data);
                free(bufpkt);
                
                // do we need to process more?
                uint16_t qlen = ringbuf_bytes_used(rx_ring_buf);
    
                // post back to our own queue with the remaining bytes
                if (qlen > 0)
                {
                    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "BT Adding leftover data message");
                    bluetooth_data_rx_notify(qlen);
                }
            }
        }
        else
        {
            
        }
    }
}

void _timer_callback(TimerHandle_t timer)
{
    /* Optionally do something if the pxTimer parameter is NULL. */
    // configASSERT(timer);
    SYS_LOG("BT", APP_LOG_LEVEL_ERROR, "packet receive buffer timeout!!");
    // Sorry, time's up
    ringbuf_reset(rx_ring_buf);

    /* Do not use a block time if calling a timer API function
    from a timer callback function, as doing so could cause a
    deadlock! */
    xTimerStop(timer, 0);
}


