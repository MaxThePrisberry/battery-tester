/*---------------------------------------------------------------------------*/
/* BatteryTester.c - Battery Testing Application for LabWindows/CVI        */
/* This application performs battery characterization and testing           */
/*---------------------------------------------------------------------------*/

#include <ansi_c.h>
#include <cvirte.h>     
#include <userint.h>
#include <utility.h>
#include <formatio.h>
#include <analysis.h>
#include <toolbox.h>
#include "BatteryTester.h"   /* Include the header file created by the UIR editor */

/* Constants */
#define MAX_DATA_POINTS 10000
#define DEFAULT_VOLTAGE_LIMIT 4.2
#define DEFAULT_CURRENT_LIMIT 2.0
#define CUTOFF_VOLTAGE 2.5

/* Global Variables */
static int panelHandle = 0;
static int testRunning = 0;
static double voltageData[MAX_DATA_POINTS];
static double currentData[MAX_DATA_POINTS];
static double capacityData[MAX_DATA_POINTS];
static double timeData[MAX_DATA_POINTS];
static int dataPoints = 0;
static int threadFunctionId = 0;
static double totalCapacity = 0.0;
static time_t testStartTime;

/* Test Parameters Structure */
typedef struct {
    double chargeVoltage;
    double chargeCurrent;
    double dischargeRate;
    double cutoffVoltage;
    int testType;  /* 0=Charge, 1=Discharge, 2=Cycle */
    int cycleCount;
} TestParameters;

static TestParameters testParams;

/* Function Prototypes */
int CVICALLBACK BatteryTestThread(void *functionData);
void InitializeApplication(void);
void CleanupApplication(void);
int PerformChargeTest(void);
int PerformDischargeTest(void);
int PerformCycleTest(void);
double SimulateBatteryVoltage(double current, double soc);
double CalculateCapacity(double current, double timeInterval);
int SaveTestResults(const char *filename);
int GenerateTestReport(void);
void UpdateDisplay(double voltage, double current, double capacity);

/*---------------------------------------------------------------------------*/
/* Main Function                                                             */
/*---------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
    /* Initialize LabWindows/CVI runtime engine */
    if (InitCVIRTE(0, argv, 0) == 0)
        return -1;    /* out of memory */
    
    /* Load the user interface panel from BatteryTester.uir */
    if ((panelHandle = LoadPanel(0, "BatteryTester.uir", PANEL)) < 0)
    {
        MessagePopup("Error", "Failed to load BatteryTester.uir panel file");
        return -1;
    }
    
    /* Initialize application */
    InitializeApplication();
    
    /* Display the panel */
    DisplayPanel(panelHandle);
    
    /* Run the user interface */
    RunUserInterface();
    
    /* Cleanup before exit */
    CleanupApplication();
    
    /* Discard the panel */
    if (panelHandle > 0)
        DiscardPanel(panelHandle);
    
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Initialize Application                                                    */
/*---------------------------------------------------------------------------*/
void InitializeApplication(void)
{
    char timeStr[256];
    
    /* Set default test parameters */
    testParams.chargeVoltage = 4.2;
    testParams.chargeCurrent = 1.0;
    testParams.dischargeRate = 0.5;
    testParams.cutoffVoltage = 2.5;
    testParams.testType = 0;
    testParams.cycleCount = 1;
    
    /* Set default values for controls */
    SetCtrlVal(panelHandle, PANEL_NUM_CHARGE_V, testParams.chargeVoltage);
    SetCtrlVal(panelHandle, PANEL_NUM_CHARGE_I, testParams.chargeCurrent);
    SetCtrlVal(panelHandle, PANEL_NUM_DISCHARGE, testParams.dischargeRate);
    SetCtrlVal(panelHandle, PANEL_NUM_CUTOFF_V, testParams.cutoffVoltage);
    SetCtrlVal(panelHandle, PANEL_RING_TEST_TYPE, testParams.testType);
    SetCtrlVal(panelHandle, PANEL_NUM_CYCLES, testParams.cycleCount);
    
    /* Initialize voltage and current displays */
    SetCtrlVal(panelHandle, PANEL_NUM_VOLTAGE, 0.0);
    SetCtrlVal(panelHandle, PANEL_NUM_CURRENT, 0.0);
    SetCtrlVal(panelHandle, PANEL_NUM_CAPACITY, 0.0);
    
    /* Initialize graphs */
    SetCtrlAttribute(panelHandle, PANEL_GRAPH_VOLTAGE, ATTR_LABEL_TEXT, "Battery Voltage");
    SetCtrlAttribute(panelHandle, PANEL_GRAPH_VOLTAGE, ATTR_XNAME, "Time (min)");
    SetCtrlAttribute(panelHandle, PANEL_GRAPH_VOLTAGE, ATTR_YNAME, "Voltage (V)");
    
    SetCtrlAttribute(panelHandle, PANEL_GRAPH_CURRENT, ATTR_LABEL_TEXT, "Charge/Discharge Current");
    SetCtrlAttribute(panelHandle, PANEL_GRAPH_CURRENT, ATTR_XNAME, "Time (min)");
    SetCtrlAttribute(panelHandle, PANEL_GRAPH_CURRENT, ATTR_YNAME, "Current (A)");
    
    /* Set status bar */
    FormatDateTimeString(time(NULL), "%c", timeStr, 256);
    SetCtrlVal(panelHandle, PANEL_TEXT_STATUS, "Battery Tester Ready");
    
    /* Initialize LED indicators */
    SetCtrlVal(panelHandle, PANEL_LED_POWER, 1);
    SetCtrlVal(panelHandle, PANEL_LED_TESTING, 0);
    SetCtrlVal(panelHandle, PANEL_LED_ERROR, 0);
}

/*---------------------------------------------------------------------------*/
/* Cleanup Application                                                       */
/*---------------------------------------------------------------------------*/
void CleanupApplication(void)
{
    /* Stop test if running */
    if (testRunning)
    {
        testRunning = 0;
        CmtWaitForThreadPoolFunctionCompletion(DEFAULT_THREAD_POOL_HANDLE, 
                                               threadFunctionId, 
                                               OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
        CmtReleaseThreadPoolFunctionID(DEFAULT_THREAD_POOL_HANDLE, threadFunctionId);
    }
}

/*---------------------------------------------------------------------------*/
/* Panel Callback - Handle panel close event                                */
/*---------------------------------------------------------------------------*/
int CVICALLBACK PanelCallback(int panel, int event, void *callbackData,
                              int eventData1, int eventData2)
{
    switch (event)
    {
        case EVENT_CLOSE:
            if (testRunning)
            {
                if (ConfirmPopup("Confirm Exit", "A test is in progress. Exit anyway?"))
                    QuitUserInterface(0);
            }
            else
            {
                QuitUserInterface(0);
            }
            break;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Start Test Button Callback                                                */
/*---------------------------------------------------------------------------*/
int CVICALLBACK StartTestCallback(int panel, int control, int event,
                                  void *callbackData, int eventData1, int eventData2)
{
    switch (event)
    {
        case EVENT_COMMIT:
            if (!testRunning)
            {
                /* Get test parameters from UI */
                GetCtrlVal(panel, PANEL_NUM_CHARGE_V, &testParams.chargeVoltage);
                GetCtrlVal(panel, PANEL_NUM_CHARGE_I, &testParams.chargeCurrent);
                GetCtrlVal(panel, PANEL_NUM_DISCHARGE, &testParams.dischargeRate);
                GetCtrlVal(panel, PANEL_NUM_CUTOFF_V, &testParams.cutoffVoltage);
                GetCtrlVal(panel, PANEL_RING_TEST_TYPE, &testParams.testType);
                GetCtrlVal(panel, PANEL_NUM_CYCLES, &testParams.cycleCount);
                
                /* Reset data */
                dataPoints = 0;
                totalCapacity = 0.0;
                testStartTime = time(NULL);
                
                /* Start test */
                testRunning = 1;
                SetCtrlAttribute(panel, PANEL_BTN_START, ATTR_DIMMED, 1);
                SetCtrlAttribute(panel, PANEL_BTN_STOP, ATTR_DIMMED, 0);
                SetCtrlVal(panel, PANEL_LED_TESTING, 1);
                
                /* Clear graphs */
                DeleteGraphPlot(panel, PANEL_GRAPH_VOLTAGE, -1, VAL_IMMEDIATE_DRAW);
                DeleteGraphPlot(panel, PANEL_GRAPH_CURRENT, -1, VAL_IMMEDIATE_DRAW);
                
                /* Update status */
                switch (testParams.testType)
                {
                    case 0:
                        SetCtrlVal(panel, PANEL_TEXT_STATUS, "Starting charge test...");
                        break;
                    case 1:
                        SetCtrlVal(panel, PANEL_TEXT_STATUS, "Starting discharge test...");
                        break;
                    case 2:
                        SetCtrlVal(panel, PANEL_TEXT_STATUS, "Starting cycle test...");
                        break;
                }
                
                /* Start test thread */
                CmtScheduleThreadPoolFunction(DEFAULT_THREAD_POOL_HANDLE,
                                            BatteryTestThread, NULL,
                                            &threadFunctionId);
            }
            break;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Stop Test Button Callback                                                 */
/*---------------------------------------------------------------------------*/
int CVICALLBACK StopTestCallback(int panel, int control, int event,
                                 void *callbackData, int eventData1, int eventData2)
{
    switch (event)
    {
        case EVENT_COMMIT:
            if (testRunning)
            {
                /* Stop test */
                testRunning = 0;
                SetCtrlAttribute(panel, PANEL_BTN_START, ATTR_DIMMED, 0);
                SetCtrlAttribute(panel, PANEL_BTN_STOP, ATTR_DIMMED, 1);
                SetCtrlVal(panel, PANEL_LED_TESTING, 0);
                SetCtrlVal(panel, PANEL_TEXT_STATUS, "Test stopped by user");
            }
            break;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Save Results Button Callback                                              */
/*---------------------------------------------------------------------------*/
int CVICALLBACK SaveResultsCallback(int panel, int control, int event,
                                   void *callbackData, int eventData1, int eventData2)
{
    char filename[MAX_PATHNAME_LEN];
    int status;
    
    switch (event)
    {
        case EVENT_COMMIT:
            if (dataPoints > 0)
            {
                /* Get filename from user */
                status = FileSelectPopup("", "*.csv", "*.csv;*.txt", 
                                       "Save Test Results", VAL_SAVE_BUTTON,
                                       0, 0, 1, 1, filename);
                
                if (status == VAL_NEW_FILE_SELECTED || status == VAL_EXISTING_FILE_SELECTED)
                {
                    if (SaveTestResults(filename) == 0)
                    {
                        SetCtrlVal(panel, PANEL_TEXT_STATUS, "Test results saved successfully");
                    }
                    else
                    {
                        MessagePopup("Error", "Failed to save test results");
                    }
                }
            }
            else
            {
                MessagePopup("Notice", "No test data to save");
            }
            break;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Generate Report Button Callback                                           */
/*---------------------------------------------------------------------------*/
int CVICALLBACK GenerateReportCallback(int panel, int control, int event,
                                      void *callbackData, int eventData1, int eventData2)
{
    switch (event)
    {
        case EVENT_COMMIT:
            if (dataPoints > 0)
            {
                if (GenerateTestReport() == 0)
                {
                    SetCtrlVal(panel, PANEL_TEXT_STATUS, "Test report generated");
                    MessagePopup("Success", "Test report has been generated");
                }
                else
                {
                    MessagePopup("Error", "Failed to generate test report");
                }
            }
            else
            {
                MessagePopup("Notice", "No test data available for report");
            }
            break;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Exit Button Callback                                                      */
/*---------------------------------------------------------------------------*/
int CVICALLBACK ExitCallback(int panel, int control, int event,
                            void *callbackData, int eventData1, int eventData2)
{
    switch (event)
    {
        case EVENT_COMMIT:
            if (testRunning)
            {
                if (ConfirmPopup("Confirm Exit", "A test is in progress. Exit anyway?"))
                    QuitUserInterface(0);
            }
            else
            {
                QuitUserInterface(0);
            }
            break;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Battery Test Thread Function                                              */
/*---------------------------------------------------------------------------*/
int CVICALLBACK BatteryTestThread(void *functionData)
{
    int result = 0;
    
    switch (testParams.testType)
    {
        case 0:  /* Charge test */
            result = PerformChargeTest();
            break;
            
        case 1:  /* Discharge test */
            result = PerformDischargeTest();
            break;
            
        case 2:  /* Cycle test */
            result = PerformCycleTest();
            break;
    }
    
    /* Test completed */
    testRunning = 0;
    SetCtrlAttribute(panelHandle, PANEL_BTN_START, ATTR_DIMMED, 0);
    SetCtrlAttribute(panelHandle, PANEL_BTN_STOP, ATTR_DIMMED, 1);
    SetCtrlVal(panelHandle, PANEL_LED_TESTING, 0);
    
    if (result == 0)
    {
        SetCtrlVal(panelHandle, PANEL_TEXT_STATUS, "Test completed successfully");
        MessagePopup("Test Complete", "Battery test has completed successfully");
    }
    else
    {
        SetCtrlVal(panelHandle, PANEL_LED_ERROR, 1);
        SetCtrlVal(panelHandle, PANEL_TEXT_STATUS, "Test failed or was interrupted");
    }
    
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Perform Charge Test                                                       */
/*---------------------------------------------------------------------------*/
int PerformChargeTest(void)
{
    double voltage = 3.0;  /* Starting voltage */
    double current = testParams.chargeCurrent;
    double capacity = 0.0;
    double elapsedTime = 0.0;
    double soc = 0.2;  /* Starting state of charge */
    time_t currentTime;
    
    while (testRunning && voltage < testParams.chargeVoltage)
    {
        /* Simulate battery charging */
        soc += 0.001;  /* Increment state of charge */
        if (soc > 1.0) soc = 1.0;
        
        voltage = SimulateBatteryVoltage(current, soc);
        
        /* Taper current as voltage approaches limit */
        if (voltage > testParams.chargeVoltage - 0.1)
        {
            current = testParams.chargeCurrent * (testParams.chargeVoltage - voltage) / 0.1;
            if (current < 0.05) current = 0.05;  /* Minimum current */
        }
        
        /* Calculate capacity */
        capacity += CalculateCapacity(current, 0.1);
        
        /* Calculate elapsed time in minutes */
        currentTime = time(NULL);
        elapsedTime = difftime(currentTime, testStartTime) / 60.0;
        
        /* Store data */
        if (dataPoints < MAX_DATA_POINTS)
        {
            timeData[dataPoints] = elapsedTime;
            voltageData[dataPoints] = voltage;
            currentData[dataPoints] = current;
            capacityData[dataPoints] = capacity;
            dataPoints++;
        }
        
        /* Update display */
        UpdateDisplay(voltage, current, capacity);
        
        /* Small delay to simulate real measurement */
        Delay(0.1);
    }
    
    totalCapacity = capacity;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Perform Discharge Test                                                    */
/*---------------------------------------------------------------------------*/
int PerformDischargeTest(void)
{
    double voltage = 4.2;  /* Starting voltage (fully charged) */
    double current = -testParams.dischargeRate;  /* Negative for discharge */
    double capacity = 0.0;
    double elapsedTime = 0.0;
    double soc = 1.0;  /* Starting state of charge */
    time_t currentTime;
    
    while (testRunning && voltage > testParams.cutoffVoltage)
    {
        /* Simulate battery discharging */
        soc -= 0.001;  /* Decrement state of charge */
        if (soc < 0.0) soc = 0.0;
        
        voltage = SimulateBatteryVoltage(current, soc);
        
        /* Calculate capacity */
        capacity += CalculateCapacity(fabs(current), 0.1);
        
        /* Calculate elapsed time in minutes */
        currentTime = time(NULL);
        elapsedTime = difftime(currentTime, testStartTime) / 60.0;
        
        /* Store data */
        if (dataPoints < MAX_DATA_POINTS)
        {
            timeData[dataPoints] = elapsedTime;
            voltageData[dataPoints] = voltage;
            currentData[dataPoints] = current;
            capacityData[dataPoints] = capacity;
            dataPoints++;
        }
        
        /* Update display */
        UpdateDisplay(voltage, current, capacity);
        
        /* Small delay to simulate real measurement */
        Delay(0.1);
    }
    
    totalCapacity = capacity;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Perform Cycle Test                                                        */
/*---------------------------------------------------------------------------*/
int PerformCycleTest(void)
{
    int cycle;
    char statusMsg[256];
    
    for (cycle = 0; cycle < testParams.cycleCount && testRunning; cycle++)
    {
        /* Update status */
        sprintf(statusMsg, "Cycle %d of %d - Charging...", cycle + 1, testParams.cycleCount);
        SetCtrlVal(panelHandle, PANEL_TEXT_STATUS, statusMsg);
        
        /* Perform charge */
        PerformChargeTest();
        
        if (!testRunning) break;
        
        /* Update status */
        sprintf(statusMsg, "Cycle %d of %d - Discharging...", cycle + 1, testParams.cycleCount);
        SetCtrlVal(panelHandle, PANEL_TEXT_STATUS, statusMsg);
        
        /* Perform discharge */
        PerformDischargeTest();
    }
    
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Simulate Battery Voltage (for demo purposes)                             */
/*---------------------------------------------------------------------------*/
double SimulateBatteryVoltage(double current, double soc)
{
    /* Simple battery model: V = V0 + (Vmax - V0) * SOC - I * R */
    double V0 = 3.0;      /* Minimum voltage */
    double Vmax = 4.2;    /* Maximum voltage */
    double R = 0.05;      /* Internal resistance */
    
    return V0 + (Vmax - V0) * soc - current * R;
}

/*---------------------------------------------------------------------------*/
/* Calculate Capacity                                                        */
/*---------------------------------------------------------------------------*/
double CalculateCapacity(double current, double timeInterval)
{
    /* Capacity in mAh = Current (A) * Time (h) * 1000 */
    return current * (timeInterval / 60.0) * 1000.0;
}

/*---------------------------------------------------------------------------*/
/* Update Display                                                            */
/*---------------------------------------------------------------------------*/
void UpdateDisplay(double voltage, double current, double capacity)
{
    /* Update numeric displays */
    SetCtrlVal(panelHandle, PANEL_NUM_VOLTAGE, voltage);
    SetCtrlVal(panelHandle, PANEL_NUM_CURRENT, current);
    SetCtrlVal(panelHandle, PANEL_NUM_CAPACITY, capacity);
    
    /* Update graphs */
    PlotPoint(panelHandle, PANEL_GRAPH_VOLTAGE, 
              timeData[dataPoints-1], voltage,
              VAL_SOLID_CIRCLE, VAL_RED);
    
    PlotPoint(panelHandle, PANEL_GRAPH_CURRENT, 
              timeData[dataPoints-1], current,
              VAL_SOLID_CIRCLE, VAL_BLUE);
}

/*---------------------------------------------------------------------------*/
/* Save Test Results                                                         */
/*---------------------------------------------------------------------------*/
int SaveTestResults(const char *filename)
{
    FILE *file;
    int i;
    char dateStr[256];
    
    file = fopen(filename, "w");
    if (file == NULL)
        return -1;
    
    /* Write header */
    FormatDateTimeString(testStartTime, "%c", dateStr, 256);
    fprintf(file, "Battery Test Results\n");
    fprintf(file, "Test Date: %s\n", dateStr);
    fprintf(file, "Test Type: ");
    switch (testParams.testType)
    {
        case 0: fprintf(file, "Charge\n"); break;
        case 1: fprintf(file, "Discharge\n"); break;
        case 2: fprintf(file, "Cycle\n"); break;
    }
    fprintf(file, "Total Capacity: %.2f mAh\n\n", totalCapacity);
    
    /* Write column headers */
    fprintf(file, "Time (min),Voltage (V),Current (A),Capacity (mAh)\n");
    
    /* Write data */
    for (i = 0; i < dataPoints; i++)
    {
        fprintf(file, "%.2f,%.3f,%.3f,%.2f\n", 
                timeData[i], voltageData[i], currentData[i], capacityData[i]);
    }
    
    fclose(file);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Generate Test Report                                                      */
/*---------------------------------------------------------------------------*/
int GenerateTestReport(void)
{
    FILE *file;
    char filename[MAX_PATHNAME_LEN];
    char dateStr[256];
    double maxVoltage, minVoltage, avgCurrent;
    
    /* Create report filename */
    FormatDateTimeString(time(NULL), "BatteryReport_%Y%m%d_%H%M%S.txt", filename, MAX_PATHNAME_LEN);
    
    file = fopen(filename, "w");
    if (file == NULL)
        return -1;
    
    /* Calculate statistics */
    MaxMin1D(voltageData, dataPoints, &maxVoltage, NULL, &minVoltage, NULL);
    Mean(currentData, dataPoints, &avgCurrent);
    
    /* Write report */
    FormatDateTimeString(testStartTime, "%c", dateStr, 256);
    fprintf(file, "=====================================\n");
    fprintf(file, "    BATTERY TEST REPORT\n");
    fprintf(file, "=====================================\n\n");
    fprintf(file, "Test Date: %s\n", dateStr);
    fprintf(file, "Test Duration: %.1f minutes\n\n", timeData[dataPoints-1]);
    
    fprintf(file, "Test Parameters:\n");
    fprintf(file, "  Charge Voltage: %.2f V\n", testParams.chargeVoltage);
    fprintf(file, "  Charge Current: %.2f A\n", testParams.chargeCurrent);
    fprintf(file, "  Discharge Rate: %.2f C\n", testParams.dischargeRate);
    fprintf(file, "  Cutoff Voltage: %.2f V\n\n", testParams.cutoffVoltage);
    
    fprintf(file, "Test Results:\n");
    fprintf(file, "  Total Capacity: %.2f mAh\n", totalCapacity);
    fprintf(file, "  Max Voltage: %.3f V\n", maxVoltage);
    fprintf(file, "  Min Voltage: %.3f V\n", minVoltage);
    fprintf(file, "  Average Current: %.3f A\n\n", avgCurrent);
    
    fprintf(file, "=====================================\n");
    
    fclose(file);
    return 0;
}