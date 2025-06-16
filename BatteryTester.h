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
#define  PANEL_LED_REMOTE_MODE            2       /* control type: LED, callback function: (none) */
#define  PANEL_NUM_SET_VOLTAGE            3       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_SET_CURRENT            4       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_CURRENT                5       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_VOLTAGE                6       /* control type: numeric, callback function: (none) */
#define  PANEL_BTN_SET_VALUES             7       /* control type: command, callback function: SetValuesCallback */
#define  PANEL_TOGGLE_REMOTE_MODE         8       /* control type: binary, callback function: RemoteModeToggle */
#define  PANEL_BTN_TEST_PSB               9       /* control type: command, callback function: TestButtonCallback */
#define  PANEL_STRING_STATUS              10      /* control type: string, callback function: (none) */
#define  PANEL_BTN_TEST_BIOLOGIC          11      /* control type: command, callback function: ConnectBiologicCallback */
#define  PANEL_STATUS_TEXT                12      /* control type: string, callback function: (none) */


     /* Control Arrays: */

          /* (no control arrays in the resource file) */


     /* Menu Bars, Menus, and Menu Items: */

          /* (no menu bars in the resource file) */


     /* Callback Prototypes: */

int  CVICALLBACK ConnectBiologicCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK PanelCallback(int panel, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK RemoteModeToggle(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK SetValuesCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestButtonCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);


#ifdef __cplusplus
    }
#endif