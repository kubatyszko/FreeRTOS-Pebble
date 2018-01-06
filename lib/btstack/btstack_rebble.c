#include "btstack_rebble.h"
#include "btstack.h"
// #include "btstack_debug.h"
#include "btstack_chipset_cc256x.h"
// #include "btstack_run_loop_embedded.h"
#include "rebbleos.h"

// UART configuration
static const hci_transport_config_uart_t config = {
    HCI_TRANSPORT_CONFIG_UART,
    115200,
    0,  // main baud rate = initial baud rate
    1,  // use flow control
    NULL
};


static btstack_packet_callback_registration_t hci_event_callback_registration;


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

    // hand over to BTstack example code (we hope)
    btstack_main(0, NULL);
    // go
    
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

// hal_uart_dma.c implementation
#include "hal_uart_dma.h"

static void dummy_handler(void);
static void dummy_handler(void){};
// handlers
static void (*rx_done_handler)(void) = &dummy_handler;
static void (*tx_done_handler)(void) = &dummy_handler;
static void (*cts_irq_handler)(void) = &dummy_handler;


void hal_uart_dma_set_sleep(uint8_t sleep){
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

static void packet_handler (uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    if (packet_type != HCI_EVENT_PACKET) return;
    switch(hci_event_packet_get_type(packet))
    {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
            printf("BTstack up and running.\n");
            break;
        case HCI_EVENT_COMMAND_COMPLETE:
            if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_read_local_version_information))
            {
                uint16_t manufacturer   = little_endian_read_16(packet, 10);
                uint16_t lmp_subversion = little_endian_read_16(packet, 12);
                // assert manufacturer is TI
                if (manufacturer != BLUETOOTH_COMPANY_ID_TEXAS_INSTRUMENTS_INC){
                    printf("ERROR: Expected Bluetooth Chipset from TI but got manufacturer 0x%04x\n", manufacturer);
                    break;
                }
                // assert correct init script is used based on expected lmp_subversion
                if (lmp_subversion != btstack_chipset_cc256x_lmp_subversion()){
                    printf("Error: LMP Subversion does not match initscript! ");
                    printf("Your initscripts is for %s chipset\n", btstack_chipset_cc256x_lmp_subversion() < lmp_subversion ? "an older" : "a newer");
                    printf("Please update Makefile to include the appropriate bluetooth_init_cc256???.c file\n");
                    break;
                }
            }
            break;
        default:
            break;
    }
}
