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
#define  PANEL_BTN_DTB_2_RUN_STOP         3       /* control type: command, callback function: DTB2RunStopCallback */
#define  PANEL_BTN_DTB_1_RUN_STOP         4       /* control type: command, callback function: DTB1RunStopCallback */
#define  PANEL_LED_REMOTE_MODE            5       /* control type: LED, callback function: (none) */
#define  PANEL_NUM_SET_CHARGE_V           6       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_SET_DISCHARGE_V        7       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_SET_CHARGE_I           8       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_SET_DISCHARGE_I        9       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_POWER                  10      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_CURRENT                11      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_VOLTAGE                12      /* control type: numeric, callback function: (none) */
#define  PANEL_TOGGLE_REMOTE_MODE         13      /* control type: binary, callback function: RemoteModeToggle */
#define  PANEL_BTN_TEST_QUEUE             14      /* control type: command, callback function: TestDeviceQueueCallback */
#define  PANEL_BTN_TEST_PSB               15      /* control type: command, callback function: TestPSBCallback */
#define  PANEL_STR_PSB_STATUS             16      /* control type: string, callback function: (none) */
#define  PANEL_BTN_TEST_BIOLOGIC          17      /* control type: command, callback function: TestBiologicCallback */
#define  PANEL_STR_BIOLOGIC_STATUS        18      /* control type: string, callback function: (none) */
#define  PANEL_DEC_BAT_CONSTS             19      /* control type: deco, callback function: (none) */
#define  PANEL_BAT_CONSTS_LABEL_2         20      /* control type: textMsg, callback function: (none) */
#define  PANEL_BAT_CONSTS_LABEL_3         21      /* control type: textMsg, callback function: (none) */
#define  PANEL_LED_BIOLOGIC_STATUS        22      /* control type: LED, callback function: (none) */
#define  PANEL_LED_PSB_STATUS             23      /* control type: LED, callback function: (none) */
#define  PANEL_DEC_STATUS                 24      /* control type: deco, callback function: (none) */
#define  PANEL_CONTROL_LABEL              25      /* control type: textMsg, callback function: (none) */
#define  PANEL_STATUS_LABEL               26      /* control type: textMsg, callback function: (none) */
#define  PANEL_CMD_PROMPT_TEXTBOX         27      /* control type: textBox, callback function: (none) */
#define  PANEL_OUTPUT_TEXTBOX             28      /* control type: textBox, callback function: (none) */
#define  PANEL_EXPERIMENTS                29      /* control type: tab, callback function: (none) */
#define  PANEL_DEC_MANUAL_CONTROL         30      /* control type: deco, callback function: (none) */
#define  PANEL_GRAPH_2                    31      /* control type: graph, callback function: (none) */
#define  PANEL_DEC_GRAPHS                 32      /* control type: deco, callback function: (none) */
#define  PANEL_GRAPH_1                    33      /* control type: graph, callback function: (none) */
#define  PANEL_GRAPH_BIOLOGIC             34      /* control type: graph, callback function: (none) */
#define  PANEL_BAT_CONSTS_LABEL           35      /* control type: textMsg, callback function: (none) */
#define  PANEL_LED_DTB_2_STATUS           36      /* control type: LED, callback function: (none) */
#define  PANEL_LED_DTB_1_STATUS           37      /* control type: LED, callback function: (none) */
#define  PANEL_SPLITTER                   38      /* control type: splitter, callback function: (none) */
#define  PANEL_SPLITTER_3                 39      /* control type: splitter, callback function: (none) */
#define  PANEL_SPLITTER_2                 40      /* control type: splitter, callback function: (none) */
#define  PANEL_STR_DTB_2_STATUS           41      /* control type: string, callback function: (none) */
#define  PANEL_STR_DTB_1_STATUS           42      /* control type: string, callback function: (none) */
#define  PANEL_NUM_DTB_2_SETPOINT         43      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_DTB_1_SETPOINT         44      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_DTB_2_TEMPERATURE      45      /* control type: scale, callback function: (none) */
#define  PANEL_NUM_DTB_1_TEMPERATURE      46      /* control type: scale, callback function: (none) */
#define  PANEL_TOGGLE_TEENSY              47      /* control type: binary, callback function: TestTeensyCallback */
#define  PANEL_STR_CMD_PROMPT_INPUT       48      /* control type: string, callback function: CmdPromptInputCallback */
#define  PANEL_NUM_TC1                    49      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_TC0                    50      /* control type: numeric, callback function: (none) */
#define  PANEL_DEC_TMPCTRL                51      /* control type: deco, callback function: (none) */
#define  PANEL_DEC_BIO_GRAPH              52      /* control type: deco, callback function: (none) */
#define  PANEL_DEC_CMDPROMPT              53      /* control type: deco, callback function: (none) */
#define  PANEL_DEC_TCS                    54      /* control type: deco, callback function: (none) */

#define  PANEL_LOAD                       2
#define  PANEL_LOAD_IMG_LOGO              2       /* control type: picture, callback function: (none) */

     /* tab page panel controls */
#define  BASELINE_NUM_EIS_INTERVAL        2       /* control type: numeric, callback function: (none) */
#define  BASELINE_NUM_CURRENT_THRESHOLD   3       /* control type: numeric, callback function: (none) */
#define  BASELINE_NUM_TEMPERATURE         4       /* control type: numeric, callback function: (none) */
#define  BASELINE_NUM_INTERVAL            5       /* control type: numeric, callback function: (none) */
#define  BASELINE_BTN_BASELINE            6       /* control type: command, callback function: StartBaselineExperimentCallback */
#define  BASELINE_NUM_OUTPUT              7       /* control type: numeric, callback function: (none) */
#define  BASELINE_STR_BASELINE_STATUS     8       /* control type: string, callback function: (none) */

     /* tab page panel controls */
#define  CDC_BTN_DISCHARGE                2       /* control type: command, callback function: CDCDischargeCallback */
#define  CDC_BTN_CHARGE                   3       /* control type: command, callback function: CDCChargeCallback */
#define  CDC_NUM_CURRENT_THRESHOLD        4       /* control type: numeric, callback function: (none) */
#define  CDC_NUM_INTERVAL                 5       /* control type: numeric, callback function: (none) */


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
int  CVICALLBACK DTB1RunStopCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK DTB2RunStopCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK PanelCallback(int panel, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK RemoteModeToggle(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK StartBaselineExperimentCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestBiologicCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestDeviceQueueCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestPSBCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestTeensyCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);


#ifdef __cplusplus
    }
#endif