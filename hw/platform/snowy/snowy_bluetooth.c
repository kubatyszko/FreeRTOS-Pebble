/* snowy_bluetooth.c
 * 
 * RebbleOS
 *
 * Author: Barry Carter <barry.carter@gmail.com>
 */

#include "stm32f4xx.h"
#include "stdio.h"
#include "string.h"
#include "log.h"
#include "FreeRTOS.h"
#include <stm32f4xx_spi.h>
#include <stm32f4xx_tim.h>
#include "stm32_power.h"
#include "btstack_rebble.h"

// Init USART1
// DMA
// IRQ
// usart1 global interrupt?
// dma rx/tx
// clocks
// 

uint8_t hw_bluetooth_power_cycle(void);

#define BT_SHUTD        GPIO_Pin_12
volatile int tx_done, rx_done = 0;

const uint8_t hci_reset_bytes[] = { 0x01, 0x03, 0x0c, 0x00 };

char gbuf[10];

void _bt_reset_hci_dma(void)
{
    char buf[50];
    DRV_LOG("BT", APP_LOG_LEVEL_DEBUG, "HCI Reset DMA");
    tx_done = 0;
    
    while(USART1->SR & USART_FLAG_RXNE)
        DRV_LOG("BT", APP_LOG_LEVEL_DEBUG, "Buf has %c", USART_ReceiveData(USART1));
    
    // send magic HCI reset
    hw_bluetooth_recv_dma(gbuf, 7);
    hw_bluetooth_send_dma(hci_reset_bytes, sizeof(hci_reset_bytes));
    DRV_LOG("BT", APP_LOG_LEVEL_DEBUG, "HCI Reset Sleep");
    
    int to = 0;
    while(!tx_done)
    {
        if (to > 10000)
            break;
        DRV_LOG("BT", APP_LOG_LEVEL_DEBUG, "TX...");
        to++;
        do_delay_ms(1);
    }
    
    while (!rx_done)
    {
        if (to > 10000)
            break;
        DRV_LOG("BT", APP_LOG_LEVEL_DEBUG, "RX...");
        to++;
    }
    printf("Got ");
    for (int i = 0; i < 7; i++)
    {       
        printf("0x%0x ", gbuf[i]);
    }
    printf("\n");
}

uint8_t hw_bluetooth_init(void)
{
    stm32_power_request(STM32_POWER_APB2, RCC_APB2Periph_SYSCFG);
    stm32_power_request(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOB);
    stm32_power_request(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOA);
    // shutdown IS B12
    // USART1
    // slowclock from LSE over MCO1
    
    //GPIO_InitTypeDef gpio_init_structure;
    GPIO_InitTypeDef gpio_init_bt;
    
    
    // nSHUTD on B12
    gpio_init_bt.GPIO_Mode = GPIO_Mode_OUT;
    gpio_init_bt.GPIO_Pin =  BT_SHUTD;
    gpio_init_bt.GPIO_PuPd = GPIO_PuPd_NOPULL;
    gpio_init_bt.GPIO_Speed = GPIO_Speed_100MHz;
    gpio_init_bt.GPIO_OType = GPIO_OType_PP;
    GPIO_Init(GPIOB, &gpio_init_bt);
    
    // Other random periphs?   
    gpio_init_bt.GPIO_Mode = GPIO_Mode_OUT;
    gpio_init_bt.GPIO_Pin =  GPIO_Pin_4;
    gpio_init_bt.GPIO_PuPd = GPIO_PuPd_NOPULL;
    gpio_init_bt.GPIO_Speed = GPIO_Speed_100MHz;
    gpio_init_bt.GPIO_OType = GPIO_OType_OD;
    GPIO_Init(GPIOA, &gpio_init_bt);
    
    // Set MCO as an AF output
    gpio_init_bt.GPIO_Mode = GPIO_Mode_AF;
    gpio_init_bt.GPIO_Pin =  GPIO_Pin_8;
    gpio_init_bt.GPIO_PuPd = GPIO_PuPd_NOPULL;
    gpio_init_bt.GPIO_Speed = GPIO_Speed_100MHz;
    gpio_init_bt.GPIO_OType = GPIO_OType_PP;
    GPIO_Init(GPIOA, &gpio_init_bt);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource8, GPIO_AF_MCO);
   
    // configure DMA
     _usart1_init(115200);
    _bluetooth_dma_init();
    
    // Well, lets go for broke. Dunno what this does
    GPIO_SetBits(GPIOA, GPIO_Pin_4);
    
    if (hw_bluetooth_power_cycle())
    {
        DRV_LOG("BT", APP_LOG_LEVEL_ERROR, "Bluetooth Failed!");
        stm32_power_release(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOA);
        stm32_power_release(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOB);     
        stm32_power_release(STM32_POWER_APB2, RCC_APB2Periph_SYSCFG);
        return 1;
    }
    
    // initialise BTStack.... Go!
    bt_device_init();
    
    DRV_LOG("BT", APP_LOG_LEVEL_DEBUG, "Done...\n");

    stm32_power_release(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOA);
    stm32_power_release(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOB);     
    stm32_power_release(STM32_POWER_APB2, RCC_APB2Periph_SYSCFG);
    
    return 0;
}


// reset Bluetooth using nShutdown
uint8_t hw_bluetooth_power_cycle(void)
{
    DRV_LOG("BT", APP_LOG_LEVEL_DEBUG, "BT: Reset...");
    stm32_power_request(STM32_POWER_APB2, RCC_APB2Periph_USART1);
    stm32_power_request(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOA);
    stm32_power_request(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOB);
    stm32_power_request(STM32_POWER_APB1, RCC_APB1Periph_PWR);
     // it should go like this
    // reset the chip by pulling nSHUTD high, sleep then low
    // Then we turn on the ext slow clock 32.768khz clock (MCO1)
    // then issue an HCI reset command.
    // The BT module should give a return of 7 bytes to ACK
    // RTS (Our CTS) will also get pulled LOW to ack reset
    
    // Pull RTS High
    // GPIO_SetBits(GPIOA, GPIO_Pin_12);
    
    // Well, lets go for broke. Dunno what this does
    GPIO_SetBits(GPIOA, GPIO_Pin_4);
    
    // allow RTC access so we can play with LSE
    PWR_BackupAccessCmd(ENABLE); 
    
    DRV_LOG("BT", APP_LOG_LEVEL_DEBUG, "BT set MCO1 32khz");
    // Turn on the MCO1 clock and pump 32khz to bluetooth
    RCC_LSEConfig(RCC_LSE_ON);

    // knock knock
    while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET);
    DRV_LOG("BT", APP_LOG_LEVEL_DEBUG, "LSE Ready");
    
    // Enable power to the MCO Osc out pin
    RCC_MCO1Config(RCC_MCO1Source_LSE, RCC_MCO1Div_1);

    // datasheet says no more than 2ms for clock stabilisation. Lets go for more
    do_delay_ms(5);
    IWDG_ReloadCounter();

    // B12 LOW then HIGH (enable)
    GPIO_ResetBits(GPIOB, BT_SHUTD);
    do_delay_ms(20);    
    GPIO_SetBits(GPIOB, BT_SHUTD);
    // datasheet says at least 90ms to init
    do_delay_ms(90);
    
    // but lets sit and wait for the device to come up
    for(int i = 0; i < 10000; i++)
    {
        if(GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_11) == 0)
        {
            DRV_LOG("BT", APP_LOG_LEVEL_DEBUG, "BT: Reset Success! Took %d ms", 90 + i);
            stm32_power_release(STM32_POWER_APB2, RCC_APB2Periph_USART1);
            stm32_power_release(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOA);
            stm32_power_release(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOB);
            stm32_power_release(STM32_POWER_APB1, RCC_APB1Periph_PWR);
            return 0;
        }
        do_delay_ms(1);
    }

//     assert(GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_11) == 0 && "BT Reset Failed");
    stm32_power_release(STM32_POWER_APB2, RCC_APB2Periph_USART1);
    stm32_power_release(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOA);
    stm32_power_release(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOB);
    stm32_power_release(STM32_POWER_APB1, RCC_APB1Periph_PWR);
    DRV_LOG("BT", APP_LOG_LEVEL_DEBUG, "BT: Failed? TERMINAL!");
    return 1;
}

void _bluetooth_dma_init(void)
{
    NVIC_InitTypeDef nvic_init_struct;
    DMA_InitTypeDef dma_init_struct;
    
    // clocks
    stm32_power_request(STM32_POWER_AHB1, RCC_AHB1Periph_DMA2);

    // TX
    DMA_DeInit(DMA2_Stream7);
    DMA_StructInit(&dma_init_struct);
    dma_init_struct.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR;
    dma_init_struct.DMA_Memory0BaseAddr = (uint32_t)0;
    dma_init_struct.DMA_DIR = DMA_DIR_MemoryToPeripheral;
    dma_init_struct.DMA_Channel = DMA_Channel_4;
    dma_init_struct.DMA_BufferSize = 1;
    dma_init_struct.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma_init_struct.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma_init_struct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma_init_struct.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    dma_init_struct.DMA_Mode = DMA_Mode_Normal;
    dma_init_struct.DMA_Priority = DMA_Priority_VeryHigh;
    dma_init_struct.DMA_FIFOMode = DMA_FIFOMode_Disable;
    DMA_Init(DMA2_Stream7, &dma_init_struct);
    
    // tell the NVIC to party
    nvic_init_struct.NVIC_IRQChannel = DMA2_Stream2_IRQn;
    nvic_init_struct.NVIC_IRQChannelPreemptionPriority = 8;
    nvic_init_struct.NVIC_IRQChannelSubPriority = 0;
    nvic_init_struct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init_struct);
    
    nvic_init_struct.NVIC_IRQChannel = DMA2_Stream7_IRQn;
    nvic_init_struct.NVIC_IRQChannelPreemptionPriority = 6;
    nvic_init_struct.NVIC_IRQChannelSubPriority = 0;
    nvic_init_struct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init_struct);    

    
    stm32_power_release(STM32_POWER_AHB1, RCC_AHB1Periph_DMA2);
}

void _usart1_init(uint32_t baud)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    USART_InitTypeDef USART_InitStruct;
    NVIC_InitTypeDef nvic_init_struct;

    stm32_power_request(STM32_POWER_APB2, RCC_APB2Periph_USART1);
    stm32_power_request(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOA);
    
    // XXX shorten

    // RX (10)
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // CTS (11)
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // TX (9)
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    // RTS (12)
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_12;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    // Real BT uses flow control
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource11, GPIO_AF_USART1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource12, GPIO_AF_USART1);
    USART_DeInit(USART1);
    USART_StructInit(&USART_InitStruct);

    USART_InitStruct.USART_BaudRate = baud;
    USART_InitStruct.USART_WordLength = USART_WordLength_8b;
    USART_InitStruct.USART_StopBits = USART_StopBits_1;
    USART_InitStruct.USART_Parity = USART_Parity_No;
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_RTS_CTS;
    USART_InitStruct.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &USART_InitStruct);
    
    USART_Cmd(USART1, ENABLE);
    
    stm32_power_release(STM32_POWER_APB2, RCC_APB2Periph_USART1);
    stm32_power_release(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOA);
}

// CTS
// display has this too (gulp)!
void EXTI15_10_IRQHandler(void){
    if (EXTI_GetITStatus(EXTI_Line11) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line11);
        DRV_LOG("BT", APP_LOG_LEVEL_DEBUG, "EXTI");
        bt_stack_cts_irq();
    }
    else if (EXTI_GetITStatus(EXTI_Line10) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line10);
        // display
    }
}

// uart1 rx
void DMA2_Stream2_IRQHandler(void)	
{
    if (DMA_GetITStatus(DMA2_Stream7, DMA_IT_TCIF2) != RESET)
    {
        DMA_ClearITPendingBit(DMA2_Stream2, DMA_IT_TCIF2);
        USART_DMACmd(USART1, USART_DMAReq_Rx, DISABLE);
        
        stm32_power_release(STM32_POWER_APB2, RCC_APB2Periph_USART1);
        stm32_power_release(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOA);
        stm32_power_release(STM32_POWER_AHB1, RCC_AHB1Periph_DMA2);
        stm32_power_release(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOE);
        stm32_power_release(STM32_POWER_APB1, RCC_APB1Periph_UART8);
        bt_stack_rx_done();
        rx_done = 1;
    }
    else
    {
        DRV_LOG("BT", APP_LOG_LEVEL_DEBUG, "DMA2 RX ERROR?");
    }
        
}

// uart1 tx 
void DMA2_Stream7_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA2_Stream7, DMA_IT_TCIF7) != RESET)
    {
        DMA_ClearITPendingBit(DMA2_Stream7, DMA_IT_TCIF7);
        USART_DMACmd(USART1, USART_DMAReq_Tx, DISABLE);

        stm32_power_release(STM32_POWER_APB2, RCC_APB2Periph_USART1);
        stm32_power_release(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOA);
        stm32_power_release(STM32_POWER_AHB1, RCC_AHB1Periph_DMA2);
        stm32_power_release(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOE);
        stm32_power_release(STM32_POWER_APB1, RCC_APB1Periph_UART8);

        bt_stack_tx_done();
        tx_done = 1;
    }

    if (DMA_GetITStatus(DMA2_Stream7, DMA_IT_TEIF7) != RESET)
    {
        DRV_LOG("BT", APP_LOG_LEVEL_DEBUG, "DMA2 TX ERROR TEIF");
    }
    if (DMA_GetITStatus(DMA2_Stream7, DMA_IT_DMEIF7) != RESET)
    {
        DRV_LOG("BT", APP_LOG_LEVEL_DEBUG, "DMA2 TX ERROR? %d", 2);
    }
    if (DMA_GetITStatus(DMA2_Stream7, DMA_IT_FEIF7) != RESET)
    {
        DRV_LOG("BT", APP_LOG_LEVEL_DEBUG, "DMA2 TX ERROR? %d", 3);
    }
}

void hw_bluetooth_set_baud(uint32_t baud)
{
    _usart1_init(baud);
}

void hw_bluetooth_enable_cts_irq()
{
    stm32_power_request(STM32_POWER_APB2, RCC_APB2Periph_SYSCFG);
    stm32_power_request(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOA);
    
    NVIC_InitTypeDef nvic_init_struct;
    EXTI_InitTypeDef exti_init_struct;

    SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOA, EXTI_PinSource11);
    
    exti_init_struct.EXTI_Line = EXTI_Line11;
    exti_init_struct.EXTI_LineCmd = ENABLE;
    exti_init_struct.EXTI_Mode = EXTI_Mode_Interrupt;
    exti_init_struct.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_Init(&exti_init_struct);

    nvic_init_struct.NVIC_IRQChannel = EXTI15_10_IRQn;
    nvic_init_struct.NVIC_IRQChannelPreemptionPriority = 7;  // must be > 5
    nvic_init_struct.NVIC_IRQChannelSubPriority = 0x00;
    nvic_init_struct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init_struct);
    DRV_LOG("BT", APP_LOG_LEVEL_DEBUG, "BT: enabled CTS irq");
    
    stm32_power_release(STM32_POWER_APB2, RCC_APB2Periph_SYSCFG);
    stm32_power_release(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOA);
}

void hw_bluetooth_disable_cts_irq()
{
    stm32_power_request(STM32_POWER_APB2, RCC_APB2Periph_SYSCFG);
    stm32_power_request(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOA);
    
    EXTI_InitTypeDef exti_init_struct;
    NVIC_InitTypeDef nvic_init_struct;
    
    exti_init_struct.EXTI_Line = EXTI_Line11;
    exti_init_struct.EXTI_LineCmd = DISABLE;
    exti_init_struct.EXTI_Mode = EXTI_Mode_Interrupt;
    exti_init_struct.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_Init(&exti_init_struct);

    // CTS used PinSource11 which is connected to EXTI15_10
    nvic_init_struct.NVIC_IRQChannel = EXTI15_10_IRQn;
    nvic_init_struct.NVIC_IRQChannelPreemptionPriority = 7;  // must be > 5
    nvic_init_struct.NVIC_IRQChannelSubPriority = 0x00;
    nvic_init_struct.NVIC_IRQChannelCmd = DISABLE;
    NVIC_Init(&nvic_init_struct);
    DRV_LOG("BT", APP_LOG_LEVEL_DEBUG, "BT: disabled CTS irq");
    
    stm32_power_release(STM32_POWER_APB2, RCC_APB2Periph_SYSCFG);
    stm32_power_release(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOA);
}

void hw_bluetooth_send_dma(uint32_t *data, uint32_t len)
{
    // XXX released in IRQ
    stm32_power_request(STM32_POWER_APB2, RCC_APB2Periph_USART1);
    stm32_power_request(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOA);
    stm32_power_request(STM32_POWER_AHB1, RCC_AHB1Periph_DMA2);
    stm32_power_request(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOE);
    stm32_power_request(STM32_POWER_APB1, RCC_APB1Periph_UART8);

    
    DMA_InitTypeDef dma_init_struct;
    NVIC_InitTypeDef nvic_init_struct;
    
    // Configure DMA controller to manage TX DMA requests
    DMA_Cmd(DMA2_Stream7, DISABLE);
    while (DMA2_Stream7->CR & DMA_SxCR_EN);

    USART_DMACmd(USART1, USART_DMAReq_Tx, DISABLE);
    DMA_DeInit(DMA2_Stream7);
    DMA_ClearFlag(DMA2_Stream7, DMA_FLAG_FEIF7|DMA_FLAG_DMEIF7|DMA_FLAG_TEIF7|DMA_FLAG_HTIF7|DMA_FLAG_TCIF7);

    DMA_StructInit(&dma_init_struct);
    // set the pointer to the USART DR register
    dma_init_struct.DMA_Channel = DMA_Channel_4;
    dma_init_struct.DMA_PeripheralBaseAddr = (uint32_t)&(USART1->DR);
    dma_init_struct.DMA_Memory0BaseAddr = (uint32_t)data;
    dma_init_struct.DMA_BufferSize = len;
    dma_init_struct.DMA_DIR = DMA_DIR_MemoryToPeripheral;
    dma_init_struct.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma_init_struct.DMA_Mode = DMA_Mode_Normal;
    dma_init_struct.DMA_PeripheralInc  = DMA_PeripheralInc_Disable;
    dma_init_struct.DMA_FIFOMode  = DMA_FIFOMode_Disable;
    dma_init_struct.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    dma_init_struct.DMA_PeripheralDataSize = DMA_MemoryDataSize_Byte;
    dma_init_struct.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    dma_init_struct.DMA_Priority = DMA_Priority_Low;
    DMA_Init(DMA2_Stream7, &dma_init_struct);
    
    NVIC_EnableIRQ(DMA2_Stream7_IRQn);
    USART_Cmd(USART1, ENABLE);
    DMA_Cmd(DMA2_Stream7, ENABLE);
    USART_DMACmd(USART1, USART_DMAReq_Tx, ENABLE);
    DMA_ITConfig(DMA2_Stream7, DMA_IT_TC, ENABLE);
}

void hw_bluetooth_recv_dma(uint32_t *data, size_t len)
{
    DMA_InitTypeDef dma_init_struct;

    stm32_power_request(STM32_POWER_APB2, RCC_APB2Periph_USART1);
    stm32_power_request(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOA);
    stm32_power_request(STM32_POWER_AHB1, RCC_AHB1Periph_DMA2);
    stm32_power_request(STM32_POWER_AHB1, RCC_AHB1Periph_GPIOE);
    stm32_power_request(STM32_POWER_APB1, RCC_APB1Periph_UART8);
    
    // Configure DMA controller to manage TX DMA requests
    DMA_Cmd(DMA2_Stream2, DISABLE);
    while (DMA2_Stream2->CR & DMA_SxCR_EN);

    DMA_ClearFlag(DMA2_Stream2, DMA_FLAG_FEIF2|DMA_FLAG_DMEIF2|DMA_FLAG_TEIF2|DMA_FLAG_HTIF2|DMA_FLAG_TCIF2);
    DMA_StructInit(&dma_init_struct);
    // set the pointer to the USART DR register
    dma_init_struct.DMA_PeripheralBaseAddr = (uint32_t) &USART1->DR;
    dma_init_struct.DMA_Channel = DMA_Channel_4;
    dma_init_struct.DMA_DIR = DMA_DIR_PeripheralToMemory;
    dma_init_struct.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma_init_struct.DMA_Memory0BaseAddr = (uint32_t)data;
    dma_init_struct.DMA_BufferSize = len;
    dma_init_struct.DMA_PeripheralInc  = DMA_PeripheralInc_Disable;
    dma_init_struct.DMA_FIFOMode  = DMA_FIFOMode_Disable;
    dma_init_struct.DMA_PeripheralDataSize = DMA_MemoryDataSize_Byte;
    dma_init_struct.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    dma_init_struct.DMA_Priority = DMA_Priority_High;
    DMA_Init(DMA2_Stream2, &dma_init_struct);
    
    DMA_Cmd(DMA2_Stream2, ENABLE);
    USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);
    DMA_ITConfig(DMA2_Stream2, DMA_IT_TC, ENABLE);
}

ssize_t _bt_write(const void *buf, size_t len)
{
    int i;
    for (i = 0; i < len; i++)
    {
      while (!(USART1->SR & USART_FLAG_TXE));
//       USART1->DR =  ((uint8_t *) buf)[i];
      USART_SendData(USART1, ((uint8_t *) buf)[i]);
    }
    
    return i;
}

ssize_t _bt_read(void *buf, size_t len)
{
    int i;
    for (i = 0; i < len; i++)
    {
        
      while (!(USART1->SR & USART_FLAG_RXNE));
      ((uint8_t *) buf)[i] = USART_ReceiveData(USART1);
//       ((uint8_t *) buf)[i] = USART1->DR & 0xff;
    }
    return i;
}


// Do delay for nTime milliseconds
void do_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
    return;
   
}


