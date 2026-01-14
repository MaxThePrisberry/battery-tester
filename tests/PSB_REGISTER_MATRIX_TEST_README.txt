================================================================================
PSB 10000 REGISTER MATRIX TEST - USAGE GUIDE
================================================================================
Date: 2026-01-13
Files: psb10000_test.h, psb10000_test.c

================================================================================
OVERVIEW
================================================================================

This comprehensive test systematically evaluates PSB register configurations to
empirically determine mode selection behavior. After 40+ debugging commits, this
test will provide definitive data on how the PSB responds to different register
settings.

WHAT IT TESTS:
- CV mode with varying power limits (20W - 1224W) - find CP mode threshold
- CV mode with varying decoy values (0A - 20A) - validate decoy theory
- CV mode with secondary setpoints - test interference
- CC mode with safe currents (0.5A - 3A MAX)
- CP mode with safe powers (5W - 20W MAX)

TOTAL: ~40 test cases, ~20 minutes runtime

================================================================================
SAFETY FEATURES
================================================================================

SAFE LIMITS ENFORCED:
- CC mode: MAX 3A current setpoint (safe for testing)
- CP mode: MAX 20W power setpoint (safe for testing)
- Emergency abort if current > 10A or voltage > 50V or power > 100W
- User can cancel anytime

WHAT IS NOT TESTED (deliberately excluded for safety):
- High current CC mode (>3A)
- High power CP mode (>20W)
- "Chaos" configurations with all registers set high

================================================================================
HOW TO RUN
================================================================================

METHOD 1: Call from code

    Add to your test suite or create a standalone caller:

    ```c
    char errorMsg[256];
    int result = Test_RegisterMatrix(errorMsg, sizeof(errorMsg));
    if (result != SUCCESS) {
        LogError("Register matrix test failed: %s", errorMsg);
    }
    ```

METHOD 2: Command-line interface (if you add BIO cmd_prompt support)

    From cmd_prompt:
    ```
    PSB REGTEST
    ```

METHOD 3: Dedicated UI button (recommended)

    Add to BatteryTester.uir a new button:
    - Label: "PSB Register Matrix Test"
    - Callback: TestPSBRegisterMatrixCallback

    Implement callback:
    ```c
    int CVICALLBACK TestPSBRegisterMatrixCallback (int panel, int control, int event,
                                                    void *callbackData, int eventData1, int eventData2) {
        if (event != EVENT_COMMIT) return 0;

        // Confirm with user
        int response = ConfirmPopup("PSB Register Matrix Test",
                                     "This will run ~40 test cases over ~20 minutes.\n\n"
                                     "The test will systematically evaluate PSB register\n"
                                     "configurations to determine mode selection behavior.\n\n"
                                     "Safe limits enforced (MAX 3A, MAX 20W).\n\n"
                                     "Continue?");
        if (!response) return 0;

        // Run test in background thread
        CmtScheduleThreadPoolFunction(DEFAULT_THREAD_POOL_HANDLE,
                                      PSBRegisterMatrixWorkerThread,
                                      NULL, NULL);
        return 0;
    }

    int CVICALLBACK PSBRegisterMatrixWorkerThread(void *functionData) {
        char errorMsg[256];
        int result = Test_RegisterMatrix(errorMsg, sizeof(errorMsg));

        // Show completion popup
        if (result == SUCCESS) {
            MessagePopup("Test Complete", errorMsg);
        } else {
            MessagePopup("Test Failed", errorMsg);
        }

        return 0;
    }
    ```

================================================================================
OUTPUT FILES
================================================================================

The test generates two files:

1. CSV FILE: psb_register_matrix_YYYYMMDD_HHMMSS.csv
   - Machine-readable data
   - Every register value, mode before/after, measurements
   - Import into Excel/Python for analysis

   Location: C:\Users\nrasm\Documents\battery_tester\

2. SUMMARY FILE: psb_register_summary_YYYYMMDD_HHMMSS.txt
   - Human-readable analysis
   - Key findings
   - Test-by-test breakdown

   Location: C:\Users\nrasm\Documents\battery_tester\

================================================================================
INTERPRETING RESULTS
================================================================================

KEY QUESTIONS THE TEST ANSWERS:

1. Power Limit Threshold for CP Mode:
   Look at "CV-PowerLimit" tests in summary:
   - If 20W/30W cause CP mode but 50W+ cause CV mode → threshold is ~30-50W
   - This confirms or refutes the "low power limit causes CP mode" theory

2. Decoy Value Necessity:
   Look at "CV-Decoy" tests in summary:
   - If 0A/0W decoys fail but 10A/100W decoys pass → decoys are necessary
   - If all decoy configurations pass → decoys may not matter

3. Secondary Setpoint Interference:
   Look at "CV-Secondary" tests in summary:
   - If 0A secondary passes but 0.5A/1A fail → even small secondary setpoints interfere
   - If all pass → secondary setpoints don't matter

4. Mode Switching Behavior:
   Count "mode_switched" in CSV:
   - How many tests show mode change after output enable?
   - Which configurations trigger mode switching?

EXAMPLE FINDINGS YOU MIGHT SEE:

   "Power Limit 20W: FAIL (Post-enable mode: CP, Current: 0.000A)"
   "Power Limit 30W: FAIL (Post-enable mode: CP, Current: 0.000A)"
   "Power Limit 50W: PASS (Post-enable mode: CV, Current: 1.234A)"
   "Power Limit 1224W: PASS (Post-enable mode: CV, Current: 1.456A)"

   CONCLUSION: Power limit threshold for CP mode is between 30W and 50W

================================================================================
NEXT STEPS AFTER RUNNING TEST
================================================================================

1. ANALYZE CSV DATA:
   - Import into Excel or Python
   - Create graphs: Power Limit vs Current, Decoy Value vs Mode
   - Look for clear thresholds and patterns

2. UPDATE CONFIGURATION BASED ON FINDINGS:
   - If power limit threshold is 50W → use >50W in all code
   - If decoys are necessary → document and enforce
   - If secondary setpoints cause issues → add checks to prevent

3. DOCUMENT EMPIRICAL BEHAVIORAL MODEL:
   - Write definitive PSB register configuration guide
   - Update all experiments to follow proven patterns
   - Add validation checks to prevent bad configurations

4. VALIDATE WITH ACTUAL BATTERY OPERATIONS:
   - Test CDC charge/discharge with proven config
   - Test Phase 3 charging with proven config
   - Verify long-term stability

================================================================================
TROUBLESHOOTING
================================================================================

ISSUE: Compilation errors

  - Make sure tests/psb10000_test.h has all new declarations
  - Check for missing #includes (time.h, math.h)
  - Verify all PSB queue functions are available

ISSUE: Test hangs or freezes

  - Check PSB connection (should be on COM3)
  - Verify PSB is in remote mode
  - Look for Modbus communication errors in log

ISSUE: Safety abort triggered

  - This is intentional if current > 10A, voltage > 50V, or power > 100W
  - Review the configuration that triggered abort
  - Check battery connection and state

ISSUE: All tests fail with zero current

  - Check battery is connected to PSB
  - Verify Teensy relay is connecting PSB to battery
  - Check battery voltage is reasonable (2.5V - 4.2V)

================================================================================
SUPPORT
================================================================================

For questions or issues:
1. Review ops-log files for detailed test execution logs
2. Check CSV file for raw data
3. Examine summary file for analysis

This test framework was developed after 40+ debugging commits to finally
provide empirical data on PSB register behavior. The goal is to replace
trial-and-error with systematic understanding.

================================================================================
