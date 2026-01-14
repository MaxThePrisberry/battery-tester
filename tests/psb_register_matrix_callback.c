/******************************************************************************
 * PSB REGISTER MATRIX TEST - UI INTEGRATION
 *
 * Add this code to BatteryTester.c or psb10000_test.c
 * to enable the register matrix test button.
 ******************************************************************************/

/******************************************************************************
 * Background Worker Thread for Register Matrix Test
 ******************************************************************************/

int CVICALLBACK PSBRegisterMatrixWorkerThread(void *functionData) {
    char errorMsg[512];
    int result;

    LogMessage("========================================");
    LogMessage("Starting PSB Register Matrix Test");
    LogMessage("========================================");

    // Run the test
    result = Test_RegisterMatrix(errorMsg, sizeof(errorMsg));

    // Post result to UI thread
    typedef struct {
        int success;
        char message[512];
    } ResultData;

    ResultData *resultData = (ResultData*)malloc(sizeof(ResultData));
    if (resultData) {
        resultData->success = (result == SUCCESS);
        strncpy(resultData->message, errorMsg, sizeof(resultData->message) - 1);
        resultData->message[sizeof(resultData->message) - 1] = '\0';

        PostDeferredCall(PSBRegisterMatrixCompletionCallback, resultData);
    }

    // Release busy flag
    CmtGetLock(g_busyLock);
    g_systemBusy = 0;
    CmtReleaseLock(g_busyLock);

    return 0;
}

/******************************************************************************
 * Completion Callback (runs on UI thread)
 ******************************************************************************/

void PSBRegisterMatrixCompletionCallback(void *callbackData) {
    typedef struct {
        int success;
        char message[512];
    } ResultData;

    ResultData *result = (ResultData*)callbackData;

    if (result) {
        if (result->success) {
            MessagePopup("PSB Register Matrix Test Complete", result->message);
        } else {
            MessagePopup("PSB Register Matrix Test Failed", result->message);
        }
        free(result);
    }

    // Restore button state if needed
    // SetCtrlAttribute(g_mainPanelHandle, PANEL_BTN_PSB_REGTEST, ATTR_LABEL_TEXT, "PSB Register Matrix Test");
    // SetCtrlAttribute(g_mainPanelHandle, PANEL_BTN_PSB_REGTEST, ATTR_DIMMED, 0);
}

/******************************************************************************
 * Button Callback (triggered when user clicks button)
 ******************************************************************************/

int CVICALLBACK TestPSBRegisterMatrixCallback(int panel, int control, int event,
                                               void *callbackData, int eventData1, int eventData2) {
    if (event != EVENT_COMMIT) return 0;

    // Check if system is busy
    CmtGetLock(g_busyLock);
    if (g_systemBusy) {
        CmtReleaseLock(g_busyLock);
        LogWarning("Cannot start PSB register matrix test - system is busy");
        MessagePopup("System Busy",
                     "Another operation is in progress.\n"
                     "Please wait for it to complete before starting the test.");
        return 0;
    }
    CmtReleaseLock(g_busyLock);

    // Check PSB connection
    PSB_Handle *psbHandle = PSB_QueueGetHandle(g_psbQueueMgr);
    if (!psbHandle || !psbHandle->isConnected) {
        LogError("PSB not connected - cannot run register matrix test");
        MessagePopup("PSB Not Connected",
                     "The PSB 10000 is not connected.\n"
                     "Please ensure it is connected before running the test.");
        return 0;
    }

    // Show confirmation dialog
    char confirmMsg[1024];
    snprintf(confirmMsg, sizeof(confirmMsg),
             "PSB REGISTER MATRIX TEST\n"
             "========================\n\n"
             "This will systematically test ~40 register configurations\n"
             "to empirically determine PSB mode selection behavior.\n\n"
             "Duration: ~20 minutes\n"
             "Total Tests: ~40\n\n"
             "SAFETY FEATURES:\n"
             "- CC mode limited to MAX 3A (safe)\n"
             "- CP mode limited to MAX 20W (safe)\n"
             "- Emergency abort if current > 10A or power > 100W\n\n"
             "OUTPUT FILES:\n"
             "- CSV data file (for analysis)\n"
             "- Human-readable summary\n\n"
             "This test will answer:\n"
             "1. What power limit triggers CP mode?\n"
             "2. Are decoy values (10A/100W) necessary?\n"
             "3. Do secondary setpoints interfere?\n"
             "4. When does mode switching occur?\n\n"
             "Continue?");

    int response = ConfirmPopup("PSB Register Matrix Test", confirmMsg);
    if (!response) {
        LogMessage("User cancelled PSB register matrix test");
        return 0;
    }

    // Set system busy
    CmtGetLock(g_busyLock);
    g_systemBusy = 1;
    CmtReleaseLock(g_busyLock);

    // Update button (optional - if you want to show "Running..." during test)
    // SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Running Test...");
    // SetCtrlAttribute(panel, control, ATTR_DIMMED, 1);

    // Launch test in background thread
    LogMessage("Launching PSB register matrix test in background thread...");
    CmtScheduleThreadPoolFunction(DEFAULT_THREAD_POOL_HANDLE,
                                  PSBRegisterMatrixWorkerThread,
                                  NULL, NULL);

    return 0;
}

/******************************************************************************
 * INTEGRATION INSTRUCTIONS
 ******************************************************************************/

/*
TO ADD TO YOUR APPLICATION:

1. ADD FUNCTION DECLARATIONS TO BatteryTester.h:

   int CVICALLBACK TestPSBRegisterMatrixCallback(int panel, int control, int event,
                                                  void *callbackData, int eventData1, int eventData2);
   int CVICALLBACK PSBRegisterMatrixWorkerThread(void *functionData);
   void PSBRegisterMatrixCompletionCallback(void *callbackData);

2. IN LabWindows/CVI UI EDITOR (BatteryTester.uir):

   a. Add a new Command Button to the main panel:
      - Label: "PSB Register Matrix Test"
      - Or: "PSB Reg Matrix" (if space is limited)

   b. Set the button's callback function to: TestPSBRegisterMatrixCallback

   c. Position it near the existing "Test PSB" button

   d. The UI editor will auto-generate the control ID (e.g., PANEL_BTN_PSB_REGTEST)

3. BUILD AND RUN:

   - Compile the project
   - Run the application
   - Click "PSB Register Matrix Test" button
   - Confirm the dialog
   - Wait ~20 minutes for completion
   - Check output files in: C:\Users\nrasm\Documents\battery_tester\

4. OUTPUT FILES:

   - psb_register_matrix_YYYYMMDD_HHMMSS.csv (raw data)
   - psb_register_summary_YYYYMMDD_HHMMSS.txt (analysis)

ALTERNATIVE 1: Command-line trigger (cmd_prompt):

   Type in the cmd_prompt textbox:

   PSB RTEST

   The test will run in background (~20 minutes).
   Use "PSB HELP" to see all available PSB commands.

ALTERNATIVE 2: Direct function call:

   If you want to call from code directly:

   char errorMsg[256];
   int result = Test_RegisterMatrix(errorMsg, sizeof(errorMsg));
*/
