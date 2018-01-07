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

static TaskHandle_t _bt_task;
static StackType_t _bt_task_stack[6000];
static StaticTask_t _bt_task_buf;

static void _bt_thread(void *pvParameters);


void bluetooth_init(void)
{
    
    _bt_task = xTaskCreateStatic(_bt_thread, "BT", 6000, NULL, tskIDLE_PRIORITY + 1UL, _bt_task_stack, &_bt_task_buf);
}

void bluetooth_send_serial()
{
}

/*
 * Bluetooth thread. The device is initialised here
 */
static void _bt_thread(void *pvParameters)
{
    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "Starting Bluetooth Module");
    // Get bluetooth running
    uint8_t err_code = hw_bluetooth_init();
    
    if (err_code)
    {
        // Error!
        SYS_LOG("BT", APP_LOG_LEVEL_ERROR, "Bluetooth Module DISABLED");
        rebbleos_module_set_status(MODULE_BLUETOOTH, MODULE_DISABLED, MODULE_ERROR);
        vTaskDelete(_bt_task);
        return;
    }
    
    rebbleos_module_set_status(MODULE_BLUETOOTH, MODULE_ENABLED, MODULE_NO_ERROR);
    SYS_LOG("BT", APP_LOG_LEVEL_INFO, "Bluetooth Module Started");
    
    for( ;; )
    {
        // we could do things here.
        
        vTaskDelay(portMAX_DELAY);
    }
}
