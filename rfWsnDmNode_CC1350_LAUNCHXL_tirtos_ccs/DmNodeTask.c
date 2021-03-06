/*
 * Copyright (c) 2016, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/***** Includes *****/
#include <DmNodeRadioTask.h>
#include <DmNodeTask.h>
#include <xdc/std.h>
#include <xdc/runtime/System.h>

#include <string.h>

#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Event.h>
#include <ti/sysbios/knl/Clock.h>

#include <ti/drivers/PIN.h>
#include <ti/drivers/pin/PINCC26XX.h>
#include <ti/display/Display.h>
#include <ti/display/DisplayExt.h>
#include "extflash/ExtFlash.h"

/* Board Header files */
#include "Board.h"
#include "SceAdc.h"

#ifdef DEVICE_FAMILY
    #undef DEVICE_FAMILY_PATH
    #define DEVICE_FAMILY_PATH(x) <ti/devices/DEVICE_FAMILY/x>
    #include DEVICE_FAMILY_PATH(driverlib/aon_batmon.h)
    #include DEVICE_FAMILY_PATH(driverlib/aux_adc.h) //for ADC calibration operations
#else
    #error "You must define DEVICE_FAMILY at the project level as one of cc26x0, cc26x0r2, cc13x0, etc."
#endif

/***** Defines *****/
#define NODE_TASK_STACK_SIZE 1024
#define NODE_TASK_PRIORITY   3

#define NODE_EVENT_ALL              0xFFFFFFFF
#define NODE_EVENT_NEW_ADC_VALUE    (uint32_t)(1 << 0)
#define NODE_EVENT_UPDATE_LCD       (uint32_t)(1 << 1)

/***** Variable declarations *****/
static Task_Params nodeTaskParams;
Task_Struct nodeTask;    /* not static so you can see in ROV */
static uint8_t nodeTaskStack[NODE_TASK_STACK_SIZE];
Event_Struct nodeEvent;  /* not static so you can see in ROV */
static Event_Handle nodeEventHandle;
static uint16_t latestAdcValue;
static int32_t latestInternalTempValue;
static Node_BLEActiveType bleActive = Node_BLEActiveTypeNotActive;

/* Pin driver handle */
static PIN_Handle buttonPinHandle;
PIN_Handle ledPinHandle;

/* Global memory storage for a PIN_Config table */
static PIN_State buttonPinState;
static PIN_State ledPinState;

/* Display driver handles */
static Display_Handle hDisplayLcd;

/* Enable the 3.3V power domain used by the LCD */
PIN_Config pinTable[] = {
    NODE_SUB1_ACTIVITY_LED | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    NODE_BLE_ACTIVITY_LED | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    Board_DIO1_RFSW | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    Board_DIO30_SWPWR | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};

/*
 * Application button pin configuration table:
 *   - Buttons interrupts are configured to trigger on falling edge.
 */
PIN_Config buttonPinTable[] = {
    Board_PIN_BUTTON0  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
    Board_PIN_BUTTON1  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
    PIN_TERMINATE
};

static uint8_t nodeAddress = 0;

/***** Prototypes *****/
static void nodeTaskFunction(UArg arg0, UArg arg1);
static void updateLcd(void);
void adcCallback(uint16_t adcValue);
void buttonCallback(PIN_Handle handle, PIN_Id pinId);

/***** Function definitions *****/
void NodeTask_init(void)
{
    /* Create event used internally for state changes */
    Event_Params eventParam;
    Event_Params_init(&eventParam);
    Event_construct(&nodeEvent, &eventParam);
    nodeEventHandle = Event_handle(&nodeEvent);

    /* Create the node task */
    Task_Params_init(&nodeTaskParams);
    nodeTaskParams.stackSize = NODE_TASK_STACK_SIZE;
    nodeTaskParams.priority = NODE_TASK_PRIORITY;
    nodeTaskParams.stack = &nodeTaskStack;
    Task_construct(&nodeTask, nodeTaskFunction, &nodeTaskParams, NULL);
}

static void nodeTaskFunction(UArg arg0, UArg arg1)
{
    /* Turn off external flash for low power */
    ExtFlash_open();
    ExtFlash_close();

    /* Initialize display and try to open both UART and LCD types of display. */
    Display_Params params;
    Display_Params_init(&params);
    params.lineClearMode = DISPLAY_CLEAR_BOTH;

    /* Open both an available LCD display
     * Whether the open call is successful depends on what is present in the
     * Display_config[] array of the board file.
     */
    hDisplayLcd = Display_open(Display_Type_LCD, &params);

    /* Check if the selected Display type was found and successfully opened */
    if (hDisplayLcd)
    {
        Display_printf(hDisplayLcd, 0, 0, "Waiting for ADC...");
    }

    /* Open LED pins */
    ledPinHandle = PIN_open(&ledPinState, pinTable);
    if (!ledPinHandle)
    {
        System_abort("Error initializing board 3.3V domain pins\n");
    }

    /* Start the SCE ADC task. */
    SceAdc_init();
    SceAdc_registerAdcCallback(adcCallback);
    SceAdc_start();

    buttonPinHandle = PIN_open(&buttonPinState, buttonPinTable);
    if (!buttonPinHandle)
    {
        System_abort("Error initializing button pins\n");
    }

    /* Setup callback for button pins */
    if (PIN_registerIntCb(buttonPinHandle, &buttonCallback) != 0)
    {
        System_abort("Error registering button callback function");
    }

    while (1)
    {
        /* Wait for event */
        uint32_t events = Event_pend(nodeEventHandle, 0, NODE_EVENT_ALL, BIOS_WAIT_FOREVER);

        /* If new ADC value, send this data */
        if (events & NODE_EVENT_NEW_ADC_VALUE) {
            /* Send ADC value to concentrator */
            NodeRadioTask_sendAdcData(latestAdcValue);

            /* update display */
            updateLcd();
        }

        if (events & NODE_EVENT_UPDATE_LCD) {
            /* update display */
            updateLcd();
        }
    }

}

static void updateLcd(void)
{

    /* get node address if not already done */
    if (nodeAddress == 0)
    {
        nodeAddress = nodeRadioTask_getNodeAddr();
    }

    /* print to LCD */
    Display_clear(hDisplayLcd);
    Display_printf(hDisplayLcd, 0, 0, "NodeID: 0x%02x", nodeAddress);
    Display_printf(hDisplayLcd, 1, 0, "ADC: %04d", latestAdcValue);
    Display_printf(hDisplayLcd, 2, 0, "TempA: %3.3f", FIXED2DOUBLE(FLOAT2FIXED(convertADCToTempDouble(latestAdcValue))));  // Convert to match concentrator fixed 8.8 resolution
    Display_printf(hDisplayLcd, 3, 0, "TempI: %d", latestInternalTempValue);
    Display_printf(hDisplayLcd, 5, 0, "Mik4el");

    if (bleActive == Node_BLEActiveTypeActive) {
        Display_printf(hDisplayLcd, 7, 0, "BLE Active");
    }
}

void adcCallback(uint16_t adcValue)
{
    /* Calibrate and save latest values */
    uint32_t calADC12_gain = AUXADCGetAdjustmentGain(AUXADC_REF_FIXED);
    int8_t calADC12_offset = AUXADCGetAdjustmentOffset(AUXADC_REF_FIXED);
    latestAdcValue = AUXADCAdjustValueForGainAndOffset(adcValue, calADC12_gain, calADC12_offset);
    latestInternalTempValue = AONBatMonTemperatureGetDegC();

    /* Post event */
    Event_post(nodeEventHandle, NODE_EVENT_NEW_ADC_VALUE);
}

/*
 *  ======== buttonCallback ========
 *  Pin interrupt Callback function board buttons configured in the pinTable.
 */
void buttonCallback(PIN_Handle handle, PIN_Id pinId)
{
    /* Only toggle if the button is still pushed (low) */
    CPUdelay(8000*50);

    if (PIN_getInputValue(Board_PIN_BUTTON0) == 0)
    {
        if (bleActive == Node_BLEActiveTypeActive) {
            bleActive = Node_BLEActiveTypeNotActive;
            Event_post(nodeEventHandle, NODE_EVENT_UPDATE_LCD);
        } else {
            bleActive = Node_BLEActiveTypeActive;
        }
        NodeRadioTask_toggleBLE();
        if (bleActive == Node_BLEActiveTypeActive) {
            Event_post(nodeEventHandle, NODE_EVENT_NEW_ADC_VALUE);
        }
    }
}
