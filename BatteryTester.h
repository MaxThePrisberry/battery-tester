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
#define  PANEL_BTN_DTB_RUN_STOP           2       /* control type: command, callback function: DTBRunStopCallback */
#define  PANEL_LED_REMOTE_MODE            3       /* control type: LED, callback function: (none) */
#define  PANEL_NUM_SET_CHARGE_V           4       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_SET_DISCHARGE_V        5       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_SET_CHARGE_I           6       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_SET_DISCHARGE_I        7       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_POWER                  8       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_CURRENT                9       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_VOLTAGE                10      /* control type: numeric, callback function: (none) */
#define  PANEL_TOGGLE_REMOTE_MODE         11      /* control type: binary, callback function: RemoteModeToggle */
#define  PANEL_BTN_TEST_QUEUE             12      /* control type: command, callback function: TestDeviceQueueCallback */
#define  PANEL_BTN_TEST_PSB               13      /* control type: command, callback function: TestPSBCallback */
#define  PANEL_STR_PSB_STATUS             14      /* control type: string, callback function: (none) */
#define  PANEL_BTN_TEST_GEIS              15      /* control type: command, callback function: TestGEISCallback */
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
#define  PANEL_OUTPUT_TEXTBOX             26      /* control type: textBox, callback function: (none) */
#define  PANEL_EXPERIMENTS                27      /* control type: tab, callback function: (none) */
#define  PANEL_DEC_MANUAL_CONTROL         28      /* control type: deco, callback function: (none) */
#define  PANEL_GRAPH_2                    29      /* control type: graph, callback function: (none) */
#define  PANEL_DEC_GRAPHS                 30      /* control type: deco, callback function: (none) */
#define  PANEL_GRAPH_1                    31      /* control type: graph, callback function: (none) */
#define  PANEL_GRAPH_BIOLOGIC             32      /* control type: graph, callback function: (none) */
#define  PANEL_BAT_CONSTS_LABEL           33      /* control type: textMsg, callback function: (none) */
#define  PANEL_LED_DTB_STATUS             34      /* control type: LED, callback function: (none) */
#define  PANEL_SPLITTER                   35      /* control type: splitter, callback function: (none) */
#define  PANEL_SPLITTER_3                 36      /* control type: splitter, callback function: (none) */
#define  PANEL_SPLITTER_2                 37      /* control type: splitter, callback function: (none) */
#define  PANEL_STR_DTB_STATUS             38      /* control type: string, callback function: (none) */
#define  PANEL_NUM_DTB_SETPOINT           39      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_DTB_TEMPERATURE        40      /* control type: scale, callback function: (none) */
#define  PANEL_DEC_TMPCTRL                41      /* control type: deco, callback function: (none) */
#define  PANEL_DEC_BIO_GRAPH              42      /* control type: deco, callback function: (none) */

     /* tab page panel controls */
#define  CAPACITY_BTN_EXP_CAPACITY        2       /* control type: command, callback function: StartCapacityExperimentCallback */
#define  CAPACITY_NUM_CURRENT_THRESHOLD   3       /* control type: numeric, callback function: (none) */
#define  CAPACITY_NUM_INTERVAL            4       /* control type: numeric, callback function: (none) */
#define  CAPACITY_NUM_DISCHARGE_CAP       5       /* control type: numeric, callback function: (none) */
#define  CAPACITY_NUM_CHARGE_CAP          6       /* control type: numeric, callback function: (none) */
#define  CAPACITY_CHECKBOX_RETURN_50      7       /* control type: radioButton, callback function: (none) */


     /* Control Arrays: */

#define  BATTERY_CONSTANTS_ARR            1
#define  GRAPHS_ARR                       2
#define  MANUAL_CONTROL_ARR               3
#define  STATUS_ARR                       4

     /* Menu Bars, Menus, and Menu Items: */

#define  MENUBAR                          1
#define  MENUBAR_MENU1                    2
#define  MENUBAR_MENU2                    3


     /* Callback Prototypes: */

int  CVICALLBACK DTBRunStopCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK PanelCallback(int panel, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK RemoteModeToggle(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK StartCapacityExperimentCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestBiologicCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestDeviceQueueCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestGEISCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestPSBCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);


#ifdef __cplusplus
    }
#endif