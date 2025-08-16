/**************************************************************************/
/* LabWindows/CVI User Interface Resource (UIR) Include File              */
/*                                                                        */
/* WARNING: Do not add to, delete from, or otherwise modify the contents  */
/*          of this include file.                                         */
/**************************************************************************/

#include <userint.h>

#ifdef __cplusplus
    extern "C" {
#endif

     /* Panels and Controls: */

#define  PANEL                            1       /* callback function: PanelCallback */
#define  PANEL_BTN_CMD_PROMPT_SEND        2       /* control type: command, callback function: CmdPromptSendCallback */
#define  PANEL_BTN_DTB_1_RUN_STOP         3       /* control type: command, callback function: DTBRunStopCallback */
#define  PANEL_LED_REMOTE_MODE            4       /* control type: LED, callback function: (none) */
#define  PANEL_NUM_SET_CHARGE_V           5       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_SET_DISCHARGE_V        6       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_SET_CHARGE_I           7       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_SET_DISCHARGE_I        8       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_POWER                  9       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_CURRENT                10      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_VOLTAGE                11      /* control type: numeric, callback function: (none) */
#define  PANEL_TOGGLE_REMOTE_MODE         12      /* control type: binary, callback function: RemoteModeToggle */
#define  PANEL_BTN_TEST_QUEUE             13      /* control type: command, callback function: TestDeviceQueueCallback */
#define  PANEL_BTN_TEST_PSB               14      /* control type: command, callback function: TestPSBCallback */
#define  PANEL_STR_PSB_STATUS             15      /* control type: string, callback function: (none) */
#define  PANEL_BTN_TEST_BIOLOGIC          16      /* control type: command, callback function: TestBiologicCallback */
#define  PANEL_STR_BIOLOGIC_STATUS        17      /* control type: string, callback function: (none) */
#define  PANEL_DEC_BAT_CONSTS             18      /* control type: deco, callback function: (none) */
#define  PANEL_BAT_CONSTS_LABEL_2         19      /* control type: textMsg, callback function: (none) */
#define  PANEL_BAT_CONSTS_LABEL_3         20      /* control type: textMsg, callback function: (none) */
#define  PANEL_LED_BIOLOGIC_STATUS        21      /* control type: LED, callback function: (none) */
#define  PANEL_LED_PSB_STATUS             22      /* control type: LED, callback function: (none) */
#define  PANEL_DEC_STATUS                 23      /* control type: deco, callback function: (none) */
#define  PANEL_CONTROL_LABEL              24      /* control type: textMsg, callback function: (none) */
#define  PANEL_STATUS_LABEL               25      /* control type: textMsg, callback function: (none) */
#define  PANEL_CMD_PROMPT_TEXTBOX         26      /* control type: textBox, callback function: (none) */
#define  PANEL_OUTPUT_TEXTBOX             27      /* control type: textBox, callback function: (none) */
#define  PANEL_EXPERIMENTS                28      /* control type: tab, callback function: (none) */
#define  PANEL_DEC_MANUAL_CONTROL         29      /* control type: deco, callback function: (none) */
#define  PANEL_GRAPH_2                    30      /* control type: graph, callback function: (none) */
#define  PANEL_DEC_GRAPHS                 31      /* control type: deco, callback function: (none) */
#define  PANEL_GRAPH_1                    32      /* control type: graph, callback function: (none) */
#define  PANEL_GRAPH_BIOLOGIC             33      /* control type: graph, callback function: (none) */
#define  PANEL_BAT_CONSTS_LABEL           34      /* control type: textMsg, callback function: (none) */
#define  PANEL_LED_DTB_1_STATUS           35      /* control type: LED, callback function: (none) */
#define  PANEL_SPLITTER                   36      /* control type: splitter, callback function: (none) */
#define  PANEL_SPLITTER_3                 37      /* control type: splitter, callback function: (none) */
#define  PANEL_SPLITTER_2                 38      /* control type: splitter, callback function: (none) */
#define  PANEL_STR_DTB_1_STATUS           39      /* control type: string, callback function: (none) */
#define  PANEL_NUM_DTB_1_SETPOINT         40      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_DTB_1_TEMPERATURE      41      /* control type: scale, callback function: (none) */
#define  PANEL_TOGGLE_TEENSY              42      /* control type: binary, callback function: TestTeensyCallback */
#define  PANEL_STR_CMD_PROMPT_INPUT       43      /* control type: string, callback function: CmdPromptInputCallback */
#define  PANEL_NUM_TC1                    44      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_TC0                    45      /* control type: numeric, callback function: (none) */
#define  PANEL_DEC_TMPCTRL                46      /* control type: deco, callback function: (none) */
#define  PANEL_DEC_BIO_GRAPH              47      /* control type: deco, callback function: (none) */
#define  PANEL_DEC_CMDPROMPT              48      /* control type: deco, callback function: (none) */
#define  PANEL_DEC_TCS                    49      /* control type: deco, callback function: (none) */

     /* tab page panel controls */
#define  BASELINE_NUM_EIS_INTERVAL        2       /* control type: numeric, callback function: (none) */
#define  BASELINE_NUM_CURRENT_THRESHOLD   3       /* control type: numeric, callback function: (none) */
#define  BASELINE_NUM_TEMPERATURE         4       /* control type: numeric, callback function: (none) */
#define  BASELINE_NUM_INTERVAL            5       /* control type: numeric, callback function: (none) */
#define  BASELINE_BTN_BASELINE            6       /* control type: command, callback function: StartBaselineExperimentCallback */
#define  BASELINE_NUM_OUTPUT              7       /* control type: numeric, callback function: (none) */
#define  BASELINE_STR_BASELINE_STATUS     8       /* control type: string, callback function: (none) */

     /* tab page panel controls */
#define  CAPACITY_BTN_EXP_CAPACITY        2       /* control type: command, callback function: StartCapacityExperimentCallback */
#define  CAPACITY_NUM_CURRENT_THRESHOLD   3       /* control type: numeric, callback function: (none) */
#define  CAPACITY_NUM_INTERVAL            4       /* control type: numeric, callback function: (none) */
#define  CAPACITY_NUM_DISCHARGE_CAP       5       /* control type: numeric, callback function: (none) */
#define  CAPACITY_NUM_CHARGE_CAP          6       /* control type: numeric, callback function: (none) */
#define  CAPACITY_CHECKBOX_RETURN_50      7       /* control type: radioButton, callback function: (none) */

     /* tab page panel controls */
#define  CDC_BTN_DISCHARGE                2       /* control type: command, callback function: CDCDischargeCallback */
#define  CDC_BTN_CHARGE                   3       /* control type: command, callback function: CDCChargeCallback */
#define  CDC_NUM_CURRENT_THRESHOLD        4       /* control type: numeric, callback function: (none) */
#define  CDC_NUM_INTERVAL                 5       /* control type: numeric, callback function: (none) */

     /* tab page panel controls */
#define  SOCEIS_NUM_EIS_INTERVAL          2       /* control type: numeric, callback function: (none) */
#define  SOCEIS_NUM_CAPACITY              3       /* control type: numeric, callback function: (none) */
#define  SOCEIS_NUM_CURRENT_THRESHOLD     4       /* control type: numeric, callback function: (none) */
#define  SOCEIS_NUM_INTERVAL              5       /* control type: numeric, callback function: (none) */
#define  SOCEIS_NUM_SOC                   6       /* control type: numeric, callback function: (none) */
#define  SOCEIS_CHECKBOX_DISCHARGE        7       /* control type: radioButton, callback function: (none) */
#define  SOCEIS_BTN_SOC_EIS               8       /* control type: command, callback function: StartSOCEISExperimentCallback */
#define  SOCEIS_BTN_IMPORT_SETTINGS       9       /* control type: command, callback function: ImportSOCEISSettingsCallback */


     /* Control Arrays: */

#define  BATTERY_CONSTANTS_ARR            1
#define  DTB_CONTROL_ARR                  2
#define  GRAPHS_ARR                       3
#define  MANUAL_CONTROL_ARR               4
#define  STATUS_ARR                       5

     /* Menu Bars, Menus, and Menu Items: */

#define  MENUBAR                          1
#define  MENUBAR_MENU1                    2
#define  MENUBAR_MENU2                    3


     /* Callback Prototypes: */

int  CVICALLBACK CDCChargeCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK CDCDischargeCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK CmdPromptInputCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK CmdPromptSendCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK DTBRunStopCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK ImportSOCEISSettingsCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK PanelCallback(int panel, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK RemoteModeToggle(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK StartBaselineExperimentCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK StartCapacityExperimentCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK StartSOCEISExperimentCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestBiologicCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestDeviceQueueCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestPSBCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestTeensyCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);


#ifdef __cplusplus
    }
#endif