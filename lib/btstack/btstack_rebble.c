#include "btstack.h"
#include "btstack_rebble.h"
#include "btstack_chipset_cc256x.h"
// #include "btstack_run_loop_embedded.h"
// hal_uart_dma.c implementation
#include "hal_uart_dma.h"
#include "rebbleos.h"
#include "btstack_spp.h"

#define RFCOMM_SERVER_CHANNEL 1
#define HEARTBEAT_PERIOD_MS 1000

static uint16_t  rfcomm_channel_id;
static uint8_t   spp_service_buffer[150];
static int       le_notification_enabled;
static hci_con_handle_t att_con_handle;

// THE Couner
static btstack_timer_source_t heartbeat;
static int  counter = 0;
static char counter_string[30];
static int  counter_string_len;

static btstack_packet_callback_registration_t hci_event_callback_registration;
static void beat(void);
static uint16_t att_read_callback(hci_con_handle_t con_handle, uint16_t att_handle, uint16_t offset, uint8_t * buffer, uint16_t buffer_size);
static int att_write_callback(hci_con_handle_t con_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size);
static void heartbeat_handler(struct btstack_timer_source *ts);

// UART configuration
static const hci_transport_config_uart_t config = {
    HCI_TRANSPORT_CONFIG_UART,
    115200,
    0,  // main baud rate = initial baud rate
    1,  // use flow control
    NULL
};

/*
 * @section Advertisements 
 *
 * @text The Flags attribute in the Advertisement Data indicates if a device is in dual-mode or not.
 * Flag 0x06 indicates LE General Discoverable, BR/EDR not supported although we're actually using BR/EDR.
 * In the past, there have been problems with Anrdoid devices when the flag was not set.
 * Setting it should prevent the remote implementation to try to use GATT over LE/EDR, which is not 
 * implemented by BTstack. So, setting the flag seems like the safer choice (while it's technically incorrect).
 */
/* LISTING_START(advertisements): Advertisement data: Flag 0x06 indicates LE-only device */
const uint8_t adv_data[] = {
    // Flags general discoverable, BR/EDR not supported
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06, 
    // Name
    0x0b, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME, 'L', 'E', ' ', 'C', 'o', 'u', 'n', 't', 'e', 'r', 
    // Incomplete List of 16-bit Service Class UUIDs -- FF10 - only valid for testing!
    0x03, BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS, 0x10, 0xff,
};
/* LISTING_END */
uint8_t adv_data_len = sizeof(adv_data);


static btstack_packet_callback_registration_t hci_event_callback_registration;


static void dummy_handler(void);
static void dummy_handler(void){};
// handlers
static void (*rx_done_handler)(void) = &dummy_handler;
static void (*tx_done_handler)(void) = &dummy_handler;
static void (*cts_irq_handler)(void) = &dummy_handler;


int btstack_main(int argc, const char ** argv);
static void packet_handler (uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

void bt_device_init(void)
{
    // init memory pools
    btstack_memory_init();
    // default run loop for embedded systems - classic while loop
    btstack_run_loop_init(btstack_run_loop_freertos_get_instance());
    // enable packet logging, at least while porting
    hci_dump_open( NULL, HCI_DUMP_STDOUT );
    // init HCI
    hci_init(hci_transport_h4_instance(btstack_uart_block_freertos_instance()), (void*) &config);
    hci_set_chipset(btstack_chipset_cc256x_instance()); // Do I need this ??
    
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);
//     hci_event_callback_registration.callback = &packet_handler;
//     hci_add_event_handler(&hci_event_callback_registration);

    
    // init L2CAP
    l2cap_init();
    
    // init RFCOMM
   
    rfcomm_init();
    rfcomm_register_service(packet_handler, RFCOMM_SERVER_CHANNEL, 0xffff);

    // init SDP, create record for SPP and register with SDP
    sdp_init();
    memset(spp_service_buffer, 0, sizeof(spp_service_buffer));
    spp_create_sdp_record(spp_service_buffer, 0x10001, RFCOMM_SERVER_CHANNEL, "SPP Counter");
    sdp_register_service(spp_service_buffer);
    SYS_LOG("BTSPP", APP_LOG_LEVEL_INFO, "SDP service record size: %u", de_get_len(spp_service_buffer));

    gap_set_local_name("SPP and LE Counter 00:00:00:00:00:00");
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_DISPLAY_YES_NO);
    gap_discoverable_control(1);

    // setup le device db
    le_device_db_init();

    // setup SM: Display only
    sm_init();

    // setup ATT server
    att_server_init(profile_data, att_read_callback, att_write_callback);    
att_server_register_packet_handler(packet_handler);


// set one-shot timer
    heartbeat.process = &heartbeat_handler;
    btstack_run_loop_set_timer(&heartbeat, HEARTBEAT_PERIOD_MS);
    btstack_run_loop_add_timer(&heartbeat);

    // setup advertisements
    uint16_t adv_int_min = 0x0030;
    uint16_t adv_int_max = 0x0030;
    uint8_t adv_type = 0;
    bd_addr_t null_addr;
    memset(null_addr, 0, 6);
    gap_advertisements_set_params(adv_int_min, adv_int_max, adv_type, 0, null_addr, 0x07, 0x00);
    gap_advertisements_set_data(adv_data_len, (uint8_t*) adv_data);
    gap_advertisements_enable(1);

    // beat once
beat();
    // hand over to BTstack example code (we hope)
//     btstack_main(0, NULL);
    // go
    
    hci_power_control(HCI_POWER_ON);
    
    btstack_run_loop_execute();
}
// BT stack needs these HAL implementations

#include "hal_time_ms.h"
uint32_t hal_time_ms(void)
{
    TickType_t tick =  xTaskGetTickCount();
    return tick * portTICK_RATE_MS;
}

// hal_cpu.h implementation
// #include "hal_cpu.h"

void hal_cpu_disable_irqs(void)
{
    __disable_irq();
}

void hal_cpu_enable_irqs(void)
{
    __enable_irq();
}

void hal_cpu_enable_irqs_and_sleep(void)
{
    __enable_irq();
    __asm__("wfe"); // go to sleep if event flag isn't set. if set, just clear it. IRQs set event flag
}


void hal_uart_dma_set_sleep(uint8_t sleep)
{
    // later..
}

// reset Bluetooth using nShutdown
void bluetooth_power_cycle(void)
{
    hw_bluetooth_power_cycle();
}

// USART complete messages

void bt_stack_tx_done()
{
    (*tx_done_handler)();
}

void bt_stack_rx_done()
{
    (*rx_done_handler)();
}

void bt_stack_cts_irq()
{
    if (cts_irq_handler)
    {
        (*cts_irq_handler)();
    }
}

void hal_uart_dma_init(void)
{
    bluetooth_power_cycle();
}

void hal_uart_dma_set_block_received( void (*the_block_handler)(void))
{
    rx_done_handler = the_block_handler;
}

void hal_uart_dma_set_block_sent( void (*the_block_handler)(void))
{
    tx_done_handler = the_block_handler;
}

void hal_uart_dma_set_csr_irq_handler( void (*the_irq_handler)(void))
{
    if(the_irq_handler)
    {
        hw_bluetooth_enable_cts_irq();
    }
    else
    {
        hw_bluetooth_disable_cts_irq();
    }
    cts_irq_handler = the_irq_handler;
}

int  hal_uart_dma_set_baud(uint32_t baud)
{
    hw_bluetooth_set_baud(baud);
    return 0;
}

void hal_uart_dma_send_block(const uint8_t *data, uint16_t size)
{
    hw_bluetooth_send_dma((uint8_t *)data, size);
}

void hal_uart_dma_receive_block(uint8_t *data, uint16_t size)
{
    hw_bluetooth_recv_dma((uint8_t *)data, size);
}

// extern void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

static void packet_handler_gap (uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    if (packet_type != HCI_EVENT_PACKET) return;
    switch(hci_event_packet_get_type(packet))
    {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
            SYS_LOG("BTSPP", APP_LOG_LEVEL_INFO, "BTstack up and running.");
            break;
        case HCI_EVENT_COMMAND_COMPLETE:
            if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_read_local_version_information))
            {
                uint16_t manufacturer   = little_endian_read_16(packet, 10);
                uint16_t lmp_subversion = little_endian_read_16(packet, 12);
                // assert manufacturer is TI
                if (manufacturer != BLUETOOTH_COMPANY_ID_TEXAS_INSTRUMENTS_INC){
                    SYS_LOG("BTSPP", APP_LOG_LEVEL_INFO, "ERROR: Expected Bluetooth Chipset from TI but got manufacturer 0x%04x", manufacturer);
                    break;
                }
                // assert correct init script is used based on expected lmp_subversion
                if (lmp_subversion != btstack_chipset_cc256x_lmp_subversion()){
                    SYS_LOG("BTSPP", APP_LOG_LEVEL_INFO, "Error: LMP Subversion does not match initscript! ");
                    SYS_LOG("BTSPP", APP_LOG_LEVEL_INFO, "Your initscripts is for %s chipset", btstack_chipset_cc256x_lmp_subversion() < lmp_subversion ? "an older" : "a newer");
                    SYS_LOG("BTSPP", APP_LOG_LEVEL_INFO, "Please update Makefile to include the appropriate bluetooth_init_cc256???.c file");
                    break;
                }
            }
            break;
        default:
            break;
    }
}




// ATT Client Read Callback for Dynamic Data
// - if buffer == NULL, don't copy data, just return size of value
// - if buffer != NULL, copy data and return number bytes copied
// @param offset defines start of attribute value
static uint16_t att_read_callback(hci_con_handle_t con_handle, uint16_t att_handle, uint16_t offset, uint8_t * buffer, uint16_t buffer_size){
    UNUSED(con_handle);

    if (att_handle == ATT_CHARACTERISTIC_0000FF11_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE){
        return att_read_callback_handle_blob((const uint8_t *)counter_string, buffer_size, offset, buffer, buffer_size);
    }
    return 0;
}

// write requests
static int att_write_callback(hci_con_handle_t con_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size){
    // ignore cancel sent for new connections
    if (transaction_mode == ATT_TRANSACTION_MODE_CANCEL) return 0;
    // find characteristic for handle
    switch (att_handle){
        case ATT_CHARACTERISTIC_0000FF11_0000_1000_8000_00805F9B34FB_01_CLIENT_CONFIGURATION_HANDLE:
            le_notification_enabled = little_endian_read_16(buffer, 0) == GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION;
            att_con_handle = con_handle;
            return 0;
        case ATT_CHARACTERISTIC_0000FF11_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE:
            SYS_LOG("BTSPP", APP_LOG_LEVEL_INFO, "Write on test characteristic: ");
            printf_hexdump(buffer, buffer_size);
            return 0;
        default:
            SYS_LOG("BTSPP", APP_LOG_LEVEL_INFO, "WRITE Callback, handle %04x, mode %u, offset %u, data: ", con_handle, transaction_mode, offset);
            printf_hexdump(buffer, buffer_size);
            return 0;
    }
}

static void beat(void)
{
    counter++;
    SYS_LOG("BTSPP", APP_LOG_LEVEL_INFO, "BTstack counter %04u", counter);
}


/*
 * @section Heartbeat Handler
 * 
 * @text Similar to the packet handler, the heartbeat handler is the combination of the individual ones.
 * After updating the counter, it requests an ATT_EVENT_CAN_SEND_NOW and/or RFCOMM_EVENT_CAN_SEND_NOW
 */

 /* LISTING_START(heartbeat): Combined Heartbeat handler */
static void heartbeat_handler(struct btstack_timer_source *ts){

    if (rfcomm_channel_id || le_notification_enabled) {
        beat();
    }

    if (rfcomm_channel_id){
        rfcomm_request_can_send_now_event(rfcomm_channel_id);
    }

    if (le_notification_enabled) {
        att_server_request_can_send_now_event(att_con_handle);
    }

    btstack_run_loop_set_timer(ts, HEARTBEAT_PERIOD_MS);
    btstack_run_loop_add_timer(ts);
} 
/* LISTING_END */


static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
bd_addr_t event_addr;
    uint8_t   rfcomm_channel_nr;
    uint16_t  mtu;
    int i;

    switch (packet_type) {
            case HCI_EVENT_PACKET:
                    switch (hci_event_packet_get_type(packet)) {
                        case HCI_EVENT_PIN_CODE_REQUEST:
                            // inform about pin code request
                            SYS_LOG("BTSPP", APP_LOG_LEVEL_INFO, "Pin code request - using '0000'");
                            hci_event_pin_code_request_get_bd_addr(packet, event_addr);
                            gap_pin_code_response(event_addr, "0000");
                            break;

                        case HCI_EVENT_USER_CONFIRMATION_REQUEST:
                            // inform about user confirmation request
                            SYS_LOG("BTSPP", APP_LOG_LEVEL_INFO, "SSP User Confirmation Request with numeric value '%06"PRIu32"'", little_endian_read_32(packet, 8));
                            SYS_LOG("BTSPP", APP_LOG_LEVEL_INFO, "SSP User Confirmation Auto accept");
                            break;

                        case HCI_EVENT_DISCONNECTION_COMPLETE:
                            le_notification_enabled = 0;
                            break;

                        case ATT_EVENT_CAN_SEND_NOW:
                            att_server_notify(att_con_handle, ATT_CHARACTERISTIC_0000FF11_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE, (uint8_t*) counter_string, counter_string_len);
                            break;

                        case RFCOMM_EVENT_INCOMING_CONNECTION:
                            // data: event (8), len(8), address(48), channel (8), rfcomm_cid (16)
                            rfcomm_event_incoming_connection_get_bd_addr(packet, event_addr); 
                            rfcomm_channel_nr = rfcomm_event_incoming_connection_get_server_channel(packet);
                            rfcomm_channel_id = rfcomm_event_incoming_connection_get_rfcomm_cid(packet);
                            SYS_LOG("BTSPP", APP_LOG_LEVEL_INFO, "RFCOMM channel %u requested for %s", rfcomm_channel_nr, bd_addr_to_str(event_addr));
                            rfcomm_accept_connection(rfcomm_channel_id);
                            break;
                                                
                        case RFCOMM_EVENT_CHANNEL_OPENED:
                                // data: event(8), len(8), status (8), address (48), server channel(8), rfcomm_cid(16), max frame size(16)
                                if (rfcomm_event_channel_opened_get_status(packet)) {
                                    SYS_LOG("BTSPP", APP_LOG_LEVEL_INFO, "RFCOMM channel open failed, status %u\n", rfcomm_event_channel_opened_get_status(packet));
                                } else {
                                    rfcomm_channel_id = rfcomm_event_channel_opened_get_rfcomm_cid(packet);
                                    mtu = rfcomm_event_channel_opened_get_max_frame_size(packet);
                                    SYS_LOG("BTSPP", APP_LOG_LEVEL_INFO, "RFCOMM channel open succeeded. New RFCOMM Channel ID %u, max frame size %u\n", rfcomm_channel_id, mtu);
                                }
                                break;

                        case RFCOMM_EVENT_CAN_SEND_NOW:
                            rfcomm_send(rfcomm_channel_id, (uint8_t*) counter_string, counter_string_len);
                            break;

                        case RFCOMM_EVENT_CHANNEL_CLOSED:
                            SYS_LOG("BTSPP", APP_LOG_LEVEL_INFO, "RFCOMM channel closed\n");
                            rfcomm_channel_id = 0;
                            break;
                        
                        default:
                            break;
                    }
                    break;
                    
            case RFCOMM_DATA_PACKET:
                SYS_LOG("BTSPP", APP_LOG_LEVEL_INFO, "RCV: '");
                for (i=0;i<size;i++){
//                     printf(packet[i]);
                    SYS_LOG("BTSPP", APP_LOG_LEVEL_INFO, "0x%x", packet[i]);
                }
//                 printf("'\n");
                break;

        default:
            break;
    }
}
