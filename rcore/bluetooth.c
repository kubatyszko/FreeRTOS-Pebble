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
#include "rbl_bluetooth.h"
#include "ringbuf.h"

// BT runloop
static TaskHandle_t _bt_task;
static StackType_t _bt_task_stack[4000];
static StaticTask_t _bt_task_buf;

// for the command processer
static TaskHandle_t _bt_cmd_task;
static StackType_t _bt_cmd_task_stack[configMINIMAL_STACK_SIZE];
static StaticTask_t _bt_cmd_task_buf;

static xQueueHandle _bt_cmd_queue;
static SemaphoreHandle_t _bt_tx_mutex;
static StaticSemaphore_t _bt_tx_mutex_buf;

static void _bt_thread(void *pvParameters);
static void _bt_cmd_thread(void *pvParameters);



// XXX move me

#define MAGIC_PACKET_HEADER 0xfeed
#define MAGIC_PACKET_FOOTER 0xbeef

#define PACKET_STATE_IDLE 0
#define PACKET_STATE_RX   1
#define PACKET_STATE_EXPLODED_WITH_FIRE 2

static uint8_t packet_state = PACKET_STATE_IDLE;

typedef struct pbl_transport_packet_t {
    uint16_t header;
    uint16_t protocol;
    uint16_t length;
    uint8_t *data;
    uint16_t footer;
} __attribute__((__packed__)) pbl_transport_packet;

uint8_t start_marker = 0;



#define PACKET_TYPE_RX 0
#define PACKET_TYPE_TX 1

typedef struct bt_packet_tx_t {
    uint8_t pkt_type;
    uint16_t endpoint;
    uint8_t *data;
    size_t len;
} bt_packet_tx;

bt_packet_tx packet;


// ring buffer (statically allocated)
// ringbuf_t rx_buf
#define TX_RING_BUF_SIZE 128
#define RX_RING_BUF_SIZE 128
// statically allocate the ring buffer
// uint8_t tx_ring_buf_buffer[TX_RING_BUF_SIZE];
ringbuf_t tx_ring_buf;
ringbuf_t rx_ring_buf;

void _parse_packet();

void bluetooth_init(void)
{
    _bt_task = xTaskCreateStatic(_bt_thread, "BT", 4000, NULL, tskIDLE_PRIORITY + 3UL, _bt_task_stack, &_bt_task_buf);
    _bt_cmd_task = xTaskCreateStatic(_bt_cmd_thread, "BTCmd", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 4UL, _bt_cmd_task_stack, &_bt_cmd_task_buf);

    
    _bt_tx_mutex = xSemaphoreCreateMutexStatic(&_bt_tx_mutex_buf);
    _bt_cmd_queue = xQueueCreate(1, sizeof(pbl_transport_packet *));
    
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
    // XXX TODO should probably lock here too
    // read the data out into our ring buffer
    ringbuf_memcpy_into(rx_ring_buf, data, len);
    SYS_LOG("BT", APP_LOG_LEVEL_ERROR, "RX: B 0x%x", (uint8_t)*(data));
    
    bluetooth_send_serial_raw(gotcha, sizeof(gotcha));

    // check for a a packet
    _parse_packet();
    // and stuff
}



int8_t _contains_packet_header(uint8_t *data, size_t len)
{
    for(size_t i = 0; i < len - 4; i++)
    {
        if (*(((uint16_t *)data + i)) == MAGIC_PACKET_HEADER)
        {
            uint16_t pkt_len = *(data + i + 4);
            
            // additionally check the packet size is sane
            // erm, 1k is way too masive
            // XXX TODO
            if (pkt_len > 1024)
            {
                SYS_LOG("BT", APP_LOG_LEVEL_ERROR, "RX: NOT a packet. Too big! > 1024");
                return -1;
            }
            return i;
        }
    }
    return -2;
}

int8_t _contains_packet_footer(uint8_t *data, size_t len)
{
    for(size_t i = 0; i < len - 2; i++)
    {
        if ((uint16_t)*(data + i) == MAGIC_PACKET_FOOTER)
        {
            // looks footery
            return i;
        }
    }
    
    return -1;
}

void _packet_remove_from_buffer(uint8_t *data, size_t len)
{
    // is there another packet lurking?
    size_t another_header_start = _contains_packet_header(data, len);
    if (another_header_start > 0)
    {
        // reset up to the new header
        ringbuf_memset(rx_ring_buf, 0, another_header_start);
    }
    else
    {
        // nuke it from orbit
        ringbuf_reset(rx_ring_buf);
    }
}

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
 * There is also a recursion here.
 *   When we find a packet, we check for more
 *   When we discard a back packet, we check for more
 */
void _parse_packet(void)
{
    uint8_t *buf_start = ringbuf_tail(rx_ring_buf);
    size_t qlen = ringbuf_bytes_used(rx_ring_buf);
    
    // look for a header
    int8_t header_start = _contains_packet_header(buf_start, qlen);
    
    if (header_start < 0)
    {
        SYS_LOG("BT", APP_LOG_LEVEL_ERROR, "RX: No header here");
        return;
    }
    
    // got a header. check buffer is big enough to at contain the footer in principle
    // this will do for the header and the length
    uint16_t pkt_len = *(buf_start + header_start + 4);
    // footer will be streamed after data...

    if (6 + pkt_len > qlen)
    {
        SYS_LOG("BT", APP_LOG_LEVEL_ERROR, "RX: Footer still coming %d %d", pkt_len, qlen);
        return;
    }

    uint16_t footer = *((uint16_t *)(buf_start + header_start + 6 + pkt_len));
    // we have the footer for sure now. Check validity
    if (footer != MAGIC_PACKET_FOOTER)
    {
        SYS_LOG("BT", APP_LOG_LEVEL_ERROR, "RX: No Footer? %d %d", pkt_len, qlen);
        // discard this!
        _packet_remove_from_buffer(buf_start, qlen);
        return;
    }
    
    uint8_t *pktdata = buf_start + header_start + 6;

    // copy into a new packet
    uint8_t *tmpdata = malloc(pkt_len);
    memcpy(tmpdata, pktdata, pkt_len);
    
    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "RX: GOOD packet. len %d", pkt_len);
    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "RX: %s", tmpdata);
    // Stuff goes here:
    // routing the packet and stuff.
    
    uint16_t endpoint = *(buf_start + header_start + 2);
    
    // Notify the bluetooth threads
    bluetooth_data_rx_notify((uint8_t *)tmpdata, pkt_len, endpoint);
        
    // we still need to clear the buffer
    _packet_remove_from_buffer(buf_start, qlen);
    
    // do we need to process more?
    const uint8_t *tbuf_start = ringbuf_head(rx_ring_buf);
    
    // look for a header
    header_start = _contains_packet_header(buf_start, qlen);
    
    if (header_start > 0)
        return _parse_packet();
    
    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "RX: Done");
}


void bluetooth_send(uint8_t *data, size_t len)
{
    packet.pkt_type = PACKET_TYPE_TX;
    packet.data = data;
    packet.len = len;
    xQueueSendToBack(_bt_cmd_queue, &packet, 0);
}

void bluetooth_data_rx_notify(uint8_t *data, size_t len, uint16_t endpoint)
{
    // XXX TODO LOCK ME REALLY URGENTLY Yo.
    packet.pkt_type = PACKET_TYPE_RX;
    packet.endpoint = endpoint;
    packet.data = data;
    packet.len = len;
    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "RX: Q PACKET");
    void *px_packet = &packet;
    xQueueSendToBack(_bt_cmd_queue, (void *)&px_packet, 0);
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

uint8_t notification[100];
uint8_t notification_len;

uint8_t *notification_get(void)
{
    return notification;
}

// TODO
static void _bt_cmd_thread(void *pvParameters)
{
    bt_packet_tx *pkt = NULL;
    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "BT CMD Thread started");
    
    // a test
    static char tpkt[18];
    
    *((uint16_t *)tpkt) = MAGIC_PACKET_HEADER;
    *((uint16_t *)tpkt + 2) = 1; // endpoint
    *((uint16_t *)(tpkt + 4)) = 10; // length
    memcpy(tpkt + 6, "hello.....", 10); // data
    *((uint16_t *)(tpkt + 6 + 10)) = MAGIC_PACKET_FOOTER;
        
//     bluetooth_data_rx((uint8_t *)tpkt, 18);
    
    for( ;; )
    {
        // Sit and wait for a wakeup. We will wake when we have a packet to send
        if (xQueueReceive(_bt_cmd_queue, &pkt, portMAX_DELAY))
        {
            if (pkt->pkt_type == PACKET_TYPE_TX)
            {
                bluetooth_send_serial_raw(pkt->data, pkt->len);
            }
            else if (pkt->pkt_type == PACKET_TYPE_RX)
            {
                // some data arrived. We are responsible for the memory
                SYS_LOG("BT", APP_LOG_LEVEL_INFO, "BT GOT PACKET %s", pkt->data);
                
                switch(pkt->endpoint)
                { 
                    case 1:
                        break;
                    default:   
                        // complain
                        break;
                }
                strncpy((char*)notification, (char*)pkt->data, 100);
                notification_len = pkt->len;
                appmanager_post_notification();
                
                free(pkt->data);                
            }
        }
        else
        {
            // nothing emerged from the buffer
        }
    }
}
