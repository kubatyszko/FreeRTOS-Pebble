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


static TaskHandle_t _bt_task;
static StackType_t _bt_task_stack[3000];
static StaticTask_t _bt_task_buf;

static void _bt_thread(void *pvParameters);


void bluetooth_init(void)
{
    
    _bt_task = xTaskCreateStatic(_bt_thread, "BT", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1UL, _bt_task_stack, &_bt_task_buf);
}

void bluetooth_send_serial()
{
}

/*
 * The main runloop. This will init all devices in a task mainly
 * so it can use IRQ Pri >5
 * 
 * This is the first task to run, the rest will be created at init.
 * 
 * Init is done, and then it loops to maint tasks
 */
static void _bt_thread(void *pvParameters)
{
    bluetooth_init();
    
    KERN_LOG("main", APP_LOG_LEVEL_INFO, "Starting Bluetooth Module");
    
    for( ;; )
    {
        // we could do things here.
        
        vTaskDelay(portMAX_DELAY);
    }
}
