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
#define  PANEL_BTN_EXIT                   2       /* control type: command, callback function: ExitCallback */
#define  PANEL_BTN_REPORT                 3       /* control type: command, callback function: GenerateReportCallback */
#define  PANEL_BTN_SAVE                   4       /* control type: command, callback function: SaveResultsCallback */
#define  PANEL_BTN_STOP                   5       /* control type: command, callback function: StopTestCallback */
#define  PANEL_BTN_START                  6       /* control type: command, callback function: StartTestCallback */
#define  PANEL_NUM_CYCLES                 7       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_CUTOFF_V               8       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_DISCHARGE              9       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_CHARGE_I               10      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_CHARGE_V               11      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_CAPACITY               12      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_CURRENT                13      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_VOLTAGE                14      /* control type: numeric, callback function: (none) */
#define  PANEL_RING_TEST_TYPE             15      /* control type: ring, callback function: (none) */
#define  PANEL_GRAPH_CURRENT              16      /* control type: graph, callback function: (none) */
#define  PANEL_GRAPH_VOLTAGE              17      /* control type: graph, callback function: (none) */
#define  PANEL_LED_ERROR                  18      /* control type: LED, callback function: (none) */
#define  PANEL_LED_TESTING                19      /* control type: LED, callback function: (none) */
#define  PANEL_LED_POWER                  20      /* control type: LED, callback function: (none) */
#define  PANEL_TEXT_STATUS                21      /* control type: textMsg, callback function: (none) */


     /* Control Arrays: */

          /* (no control arrays in the resource file) */


     /* Menu Bars, Menus, and Menu Items: */

          /* (no menu bars in the resource file) */


     /* Callback Prototypes: */

int  CVICALLBACK ExitCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK GenerateReportCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK PanelCallback(int panel, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK SaveResultsCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK StartTestCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK StopTestCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);


#ifdef __cplusplus
    }
#endif