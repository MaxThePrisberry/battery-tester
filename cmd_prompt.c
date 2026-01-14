/******************************************************************************
 * cmd_prompt.c
 * 
 * Command Prompt Implementation
 * Handles cmd prompt control callbacks and prompt command logic
 ******************************************************************************/

#include "BatteryTester.h"
#include "common.h"
#include "cmd_prompt.h"
#include "logging.h"
#include "controls.h"
#include "teensy_queue.h"
#include "dtb4848_queue.h"
#include "biologic_abstract.h"
#include "psb10000_queue.h"
#include "tests/psb10000_test.h"

/******************************************************************************
 * Static Functions
 ******************************************************************************/

static int CmdPromptSendThread();
static void LogPromptTextbox(enum Status status, char *message);
static void DeferredPromptTextboxUpdate(void *data);
static int DeviceSelect(CommandContext *ctx);
static int TeensyCommandManager(CommandContext *ctx);
static int DTBCommandManager(CommandContext *ctx);
static int ControlsCommandManager(CommandContext *ctx);
static int DAQCommandManager(CommandContext *ctx);
static int BioLogicCommandManager(CommandContext *ctx);
static int PSBCommandManager(CommandContext *ctx);

/******************************************************************************
 * UI panel CVICALLBACKS
 ******************************************************************************/

int CVICALLBACK CmdPromptSendCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2){
	if (event != EVENT_COMMIT) {
		return 0;
	}
	
	// Schedule the prompt processing thread
	int threadID;
	CmtScheduleThreadPoolFunction(g_threadPool, CmdPromptSendThread, NULL, &threadID);
	
	if (threadID == 0) {
		LogPromptTextbox(CMD_ERROR, "There was an error scheduling the command send thread.");
		return -1;
	}
	
	return 0;
}

int CVICALLBACK CmdPromptInputCallback (int panel, int control, int event, void *callbackData, int eventData1, int eventData2){
    if (event != EVENT_KEYPRESS || eventData1 != VAL_ENTER_VKEY) {
		return 0;
    }
	
	// Schedule the prompt processing thread
	int threadID;
	CmtScheduleThreadPoolFunction(g_threadPool, CmdPromptSendThread, NULL, &threadID);
	
	if (threadID == 0) {
		LogPromptTextbox(CMD_ERROR, "There was an error scheduling the command send thread.");
		return -1;
	}
	
    return 1;
}

/******************************************************************************
 * Helper functions
 ******************************************************************************/

// Function to print out a message to the prompt textbox
static void LogPromptTextbox(enum Status status, char *message) {
	UIUpdateData *data = malloc(sizeof(UIUpdateData));
	data->status = status;
	data->message = my_strdup(message);
	PostDeferredCall(DeferredPromptTextboxUpdate, data);
}

// Callback function for the main UI thread to update the textbox
static void DeferredPromptTextboxUpdate(void *callbackData) {
	UIUpdateData *data = (UIUpdateData*)callbackData;
	
	if (data && g_mainPanelHandle > 0 && PANEL_CMD_PROMPT_TEXTBOX > 0 && data->message) {
		char buffer[OUTPUT_BUFFER_SIZE];
		switch (data->status) {
			case CMD_ERROR:
				snprintf(buffer, OUTPUT_BUFFER_SIZE, "[ERROR] %s", data->message);
				break;
			case CMD_INPUT:
				snprintf(buffer, OUTPUT_BUFFER_SIZE, "[<<---] %s", data->message);
				break;
			case CMD_OUTPUT:
				snprintf(buffer, OUTPUT_BUFFER_SIZE, "[--->>] %s", data->message);
				break;
		}
		
		InsertTextBoxLine(g_mainPanelHandle, PANEL_CMD_PROMPT_TEXTBOX, -1, buffer);
		
		int numLines;
		GetNumTextBoxLines(g_mainPanelHandle, PANEL_CMD_PROMPT_TEXTBOX, &numLines);
		if (numLines > 0) {
			SetCtrlAttribute(g_mainPanelHandle, PANEL_CMD_PROMPT_TEXTBOX, ATTR_FIRST_VISIBLE_LINE, numLines);
		}
		
		free(data->message);
		free(data);		
	}
}

static int HexCharToInt(char c) {
	if (c >= '0' && c <= '9') return (c - '0');
	if (c >= 'A' && c <= 'F') return (c - 'A' + 10);
	if (c >= 'a' && c <= 'f') return (c - 'a' + 10);
	return -1;
}

/******************************************************************************
 * Main prompt processing thread
 ******************************************************************************/

static int CmdPromptSendThread() {
	// Create command context
	CommandContext *ctx = malloc(sizeof(CommandContext));
	memset(ctx, 0, sizeof(CommandContext));
	
	// Get the current string control content length
	int commandLength;
	GetCtrlAttribute(g_mainPanelHandle, PANEL_STR_CMD_PROMPT_INPUT, ATTR_STRING_TEXT_LENGTH, &commandLength);
	
	// Dynamically allocate memory for the command, trim it, store it in the context
	char *raw = malloc(commandLength + 1);
	GetCtrlVal(g_mainPanelHandle, PANEL_STR_CMD_PROMPT_INPUT, raw);
	char *trimmed = TrimWhitespace(raw);
	ctx->command = my_strdup(trimmed);
	free(raw);
	
	// Clear the control for the next command
	SetCtrlVal(g_mainPanelHandle, PANEL_STR_CMD_PROMPT_INPUT, "");
	
	// Check to see if the command is the appropriate length
	commandLength = strlen(ctx->command);
	ctx->commandLength = commandLength;
	if (commandLength < 4) {
		goto cleanup;
	} else if (commandLength > MESSAGE_LENGTH_LIMIT) {
		LogPromptTextbox(CMD_ERROR, "Message Length Error: The command you've entered is too long.");
		goto cleanup;
	}
	
	// Log the input to the textbox so the user can see the send/response log
	LogPromptTextbox(CMD_INPUT, ctx->command);
	
	// Pass the command to the device selector
	DeviceSelect(ctx);
	
cleanup:
	free(ctx->command);
	free(ctx);
	
	return 0;		
}

static int DeviceSelect(CommandContext *ctx) {
	// The first three letters of the command delineate which manager should handle the command
	int deviceCode = (ctx->command[0] << 16 | ctx->command[1] << 8 | ctx->command[2]);
	
	// Remove first three characters specifying device
	char *trimmed = malloc(ctx->commandLength - 2);	// Trim 3 characters but account for the \0 null termination character
	trimmed = my_strdup(&ctx->command[3]);
	free(ctx->command);
	ctx->command = trimmed;
	ctx->commandLength -= 3;
	
	// Pass the command to the appropriate device manager
	switch (deviceCode) {
		case ('T' << 16 | 'N' << 8 | 'Y'):
			TeensyCommandManager(ctx);
			break;

		case ('D' << 16 | 'T' << 8 | 'B'):
			DTBCommandManager(ctx);
			break;

		case ('C' << 16 | 'T' << 8 | 'L'):
			ControlsCommandManager(ctx);
			break;

		case ('D' << 16 | 'A' << 8 | 'Q'):
			DAQCommandManager(ctx);
			break;

		case ('B' << 16 | 'I' << 8 | 'O'):
			BioLogicCommandManager(ctx);
			break;

		case ('P' << 16 | 'S' << 8 | 'B'):
			PSBCommandManager(ctx);
			break;

		default:
			LogPromptTextbox(CMD_ERROR, "No such device.");
	}
	
	return 0;
}

/******************************************************************************
 * Device command managers
 ******************************************************************************/

static int TeensyCommandManager(CommandContext *ctx) {
	if (strlen(ctx->command) != 4) {
		LogPromptTextbox(CMD_ERROR, "Teensy serial commands must be exactly 4 characters.");
		return 0;
	}
	
	int error;
	char response[16];
	error = TNY_SendRawCommandQueued(ctx->command, response, 16, DEVICE_PRIORITY_HIGH);
	
	if (error != SUCCESS) {
		char message[1024];
		snprintf(message, 1024, "Raw command failed: %d : %s", error, GetErrorString(error));
		LogPromptTextbox(CMD_ERROR, message);
		return -1;
	}
	
	LogPromptTextbox(CMD_OUTPUT, response);
	
	return 0;
}

static int DTBCommandManager(CommandContext *ctx) {
	if (ctx->commandLength < 3) {
		LogPromptTextbox(CMD_ERROR, "DTB command too short. Specify slave hex.");
		return -1;
	}
	
	int high = HexCharToInt(ctx->command[0]);
	int low = HexCharToInt(ctx->command[1]);
	
	if (high < 0 || low < 0) {
		LogPromptTextbox(CMD_ERROR, "Invalid hex slave address given.");
		return -1;
	}
	int slaveAddress = (high << 4) | low;
	
	// Remove first two characters slave address
	char *trimmed = malloc(ctx->commandLength - 1);	// Trim 2 characters but account for the \0 null termination character
	trimmed = my_strdup(&ctx->command[2]);
	free(ctx->command);
	ctx->command = trimmed;
	ctx->commandLength -= 2;
	
	if (strcmp(ctx->command, "RESET") == 0) {
		int error = DTB_FactoryResetQueued(slaveAddress, DEVICE_PRIORITY_HIGH);
		
		if (error != SUCCESS) {
			char message[1024];
			snprintf(message, 1024, "Reset command failed: %d : %s", error, GetErrorString(error));
			LogPromptTextbox(CMD_ERROR, message);
			return -1;
		}
		
		LogPromptTextbox(CMD_OUTPUT, "Reset command success.");
	} else if (strcmp(ctx->command, "SETUP") == 0) {
		int error = DTB_EnableWriteAccessQueued(slaveAddress, DEVICE_PRIORITY_HIGH);
		
		if (error != SUCCESS) {
			char message[1024];
			snprintf(message, 1024, "Write access command failed: %d : %s", error, GetErrorString(error));
			LogPromptTextbox(CMD_ERROR, message);
			return -1;
		}
		
		error = DTB_ConfigureDefaultQueued(slaveAddress, DEVICE_PRIORITY_HIGH);
		
		if (error != SUCCESS) {
			char message[1024];
			snprintf(message, 1024, "Configure command failed: %d : %s", error, GetErrorString(error));
			LogPromptTextbox(CMD_ERROR, message);
			return -1;
		}
		
		LogPromptTextbox(CMD_OUTPUT, "Setup command success.");
	} else if (strcmp(ctx->command, "AT") == 0) {
		int error = DTB_StartAutoTuningQueued(slaveAddress, DEVICE_PRIORITY_HIGH);
		
		if (error != SUCCESS) {
			char message[1024];
			snprintf(message, 1024, "Autotune command failed: %d : %s", error, GetErrorString(error));
			LogPromptTextbox(CMD_ERROR, message);
			return -1;
		}
		
		LogPromptTextbox(CMD_OUTPUT, "Autotune command success.");
	} else {
		LogPromptTextbox(CMD_ERROR, "Invalid DTB command.");
	}
	
	return 0;
}

static int ControlsCommandManager(CommandContext *ctx) {
	if (strcmp(ctx->command, "LOAD") == 0) {
		// Try to reload the PSB and DTB values
		Controls_UpdateFromDeviceStates();		
		LogPromptTextbox(CMD_OUTPUT, "Update request completed.");
	} else {
		LogPromptTextbox(CMD_ERROR, "Invalid controls command.");
	}
	
	return 0;
}

static int DAQCommandManager(CommandContext *ctx) {
	if (0) {
		// Insert command logic here
	} else {
		LogPromptTextbox(CMD_ERROR, "Invalid DAQ command.");
	}

	return 0;
}

static int BioLogicCommandManager(CommandContext *ctx) {
	char message[1024];
	int error;

	// Skip leading space (device prefix "BIO" already stripped by DeviceSelect)
	if (ctx->command[0] == ' ') {
		char *command = my_strdup(&ctx->command[1]);
		free(ctx->command);
		ctx->command = command;
	}

	// BIO MODE - Show current control mode
	if (strcmp(ctx->command, "MODE") == 0) {
		BIO_ControlMode mode = BIO_GetCurrentMode();

		if (mode == -1) {
			LogPromptTextbox(CMD_ERROR, "BioLogic abstraction not initialized");
			return -1;
		}

		snprintf(message, sizeof(message), "Current mode: %s", BIO_GetModeName(mode));
		LogPromptTextbox(CMD_OUTPUT, message);
		return 0;
	}

	// BIO TEST - Test connection
	if (strcmp(ctx->command, "TEST") == 0) {
		error = BIO_Abstract_TestConnection();

		if (error == SUCCESS) {
			LogPromptTextbox(CMD_OUTPUT, "Connection test: OK");
		} else {
			snprintf(message, sizeof(message), "Connection test failed: %s", GetErrorString(error));
			LogPromptTextbox(CMD_ERROR, message);
		}

		return 0;
	}

	// BIO ID - Get device ID
	if (strcmp(ctx->command, "ID") == 0) {
		int deviceID = BIO_Abstract_GetDeviceID();

		if (deviceID < 0) {
			LogPromptTextbox(CMD_ERROR, "Failed to get device ID");
			return -1;
		}

		snprintf(message, sizeof(message), "Device ID: %d", deviceID);
		LogPromptTextbox(CMD_OUTPUT, message);
		return 0;
	}

	// BIO OCV - Run quick OCV test
	if (strcmp(ctx->command, "OCV") == 0) {
		LogPromptTextbox(CMD_OUTPUT, "Running OCV measurement (10s)...");

		BIO_TechniqueData *result = NULL;
		error = BIO_Abstract_RunOCV(0,          // channel 0
		                           10.0,       // 10 second duration
		                           1.0,        // 1 second interval
		                           0.0,        // no dE threshold
		                           1.0,        // 1s time threshold
		                           KBIO_ERANGE_AUTO,  // auto voltage range
		                           &result,
		                           60000,      // 60 second timeout
		                           NULL,       // no progress callback
		                           NULL,       // no user data
		                           NULL);      // no cancel flag

		if (error != SUCCESS) {
			snprintf(message, sizeof(message), "OCV failed: %s", GetErrorString(error));
			LogPromptTextbox(CMD_ERROR, message);
			return -1;
		}

		if (result && result->rawData && result->rawData->numPoints > 0) {
			snprintf(message, sizeof(message), "OCV complete: %d points collected",
			        result->rawData->numPoints);
			LogPromptTextbox(CMD_OUTPUT, message);
			BIO_FreeTechniqueData(result);
		} else {
			LogPromptTextbox(CMD_ERROR, "OCV returned no data");
		}

		return 0;
	}

	// BIO GEIS - Run quick GEIS test
	if (strcmp(ctx->command, "GEIS") == 0) {
		LogPromptTextbox(CMD_OUTPUT, "Running GEIS measurement (10kHz-0.1Hz, 500mA)...");

		BIO_TechniqueData *result = NULL;
		error = BIO_Abstract_RunGEIS(
			0,                   // channel 0
			GEIS_VS_INITIAL,     // vs_initial
			GEIS_INITIAL_CURRENT,// initial_current_step (A)
			GEIS_DURATION_S,     // duration (s)
			GEIS_RECORD_EVERY_DT,// record_every_dT (s)
			GEIS_RECORD_EVERY_DE,// record_every_dE (V)
			GEIS_INITIAL_FREQ,   // initial_freq (Hz)
			GEIS_FINAL_FREQ,     // final_freq (Hz)
			GEIS_SWEEP_LINEAR,   // sweep_linear
			GEIS_AMPLITUDE_I,    // amplitude_I (A)
			GEIS_FREQ_NUMBER,    // freq_number
			GEIS_AVERAGE_N,      // average_N
			GEIS_CORRECTION,     // correction
			GEIS_WAIT_FOR_STEADY,// wait_for_steady
			GEIS_I_RANGE,        // i_range (current range)
			&result,
			GEIS_TIMEOUT_MS,     // timeout (ms)
			NULL,                // no progress callback
			NULL,                // no user data
			NULL);               // no cancel flag

		if (error != SUCCESS) {
			snprintf(message, sizeof(message), "GEIS failed: %s", GetErrorString(error));
			LogPromptTextbox(CMD_ERROR, message);
			return -1;
		}

		if (result && result->rawData && result->rawData->numPoints > 0) {
			snprintf(message, sizeof(message), "GEIS complete: %d frequency points",
			        result->rawData->numPoints);
			LogPromptTextbox(CMD_OUTPUT, message);
			BIO_FreeTechniqueData(result);
		} else {
			LogPromptTextbox(CMD_ERROR, "GEIS returned no data");
		}

		return 0;
	}

	// BIO RECON - Test automatic reconnection (EC-Lab mode only)
	if (strcmp(ctx->command, "RECON") == 0) {
		BIO_ControlMode mode = BIO_GetCurrentMode();

		if (mode != BIO_MODE_ECLAB_OLECOM) {
			LogPromptTextbox(CMD_ERROR, "RECON command only works in EC-Lab mode");
			snprintf(message, sizeof(message), "Current mode: %s", BIO_GetModeName(mode));
			LogPromptTextbox(CMD_OUTPUT, message);
			return -1;
		}

		LogPromptTextbox(CMD_OUTPUT, "Testing EC-Lab COM reconnection...");
		LogPromptTextbox(CMD_OUTPUT, "This simulates the RPC_E_DISCONNECTED bug by recreating the COM interface");

		// Call the reconnect function directly (requires EC-Lab backend access)
		#if BIOLOGIC_CONTROL_MODE == 1  // EC-Lab mode
			extern int BIO_ECLAB_Reconnect(void);
			error = BIO_ECLAB_Reconnect();

			if (error == SUCCESS) {
				LogPromptTextbox(CMD_OUTPUT, "Reconnection test: SUCCESS");
				LogPromptTextbox(CMD_OUTPUT, "COM interface recreated and device reconnected");
			} else {
				snprintf(message, sizeof(message), "Reconnection test FAILED: %s", GetErrorString(error));
				LogPromptTextbox(CMD_ERROR, message);
			}
		#else
			LogPromptTextbox(CMD_ERROR, "Not compiled in EC-Lab mode (BIOLOGIC_CONTROL_MODE != 1)");
		#endif

		return 0;
	}

	// BIO HELP - Show help
	if (strcmp(ctx->command, "HELP") == 0) {
		LogPromptTextbox(CMD_OUTPUT, "BioLogic Abstraction Commands:");
		LogPromptTextbox(CMD_OUTPUT, "  BIO MODE  - Show current control mode (DLL/EC-Lab)");
		LogPromptTextbox(CMD_OUTPUT, "  BIO TEST  - Test connection to device");
		LogPromptTextbox(CMD_OUTPUT, "  BIO ID    - Get device ID");
		LogPromptTextbox(CMD_OUTPUT, "  BIO OCV   - Run quick 10s OCV test");
		LogPromptTextbox(CMD_OUTPUT, "  BIO GEIS  - Run quick GEIS test (10kHz-0.1Hz, 500mA)");
		LogPromptTextbox(CMD_OUTPUT, "  BIO RECON - Test EC-Lab COM reconnection (EC-Lab mode only)");
		LogPromptTextbox(CMD_OUTPUT, "  BIO HELP  - Show this help");
		LogPromptTextbox(CMD_OUTPUT, "");
		LogPromptTextbox(CMD_OUTPUT, "Note: Uses abstraction layer (auto DLL/EC-Lab mode)");
		return 0;
	}

	// Invalid command
	snprintf(message, sizeof(message), "Invalid BIO command: %s (Use: BIO HELP)", ctx->command);
	LogPromptTextbox(CMD_ERROR, message);
	return 0;
}

/******************************************************************************
 * PSB Register Matrix Test Worker Thread
 ******************************************************************************/

typedef struct {
	int success;
	char message[512];
} PSBRegTestResult;

static int PSBRegTestWorkerThread(void *functionData) {
	char errorMsg[512];
	int result;

	LogMessage("========================================");
	LogMessage("Starting PSB Register Matrix Test (cmd_prompt)");
	LogMessage("========================================");

	// Run the test
	result = Test_RegisterMatrix(errorMsg, sizeof(errorMsg));

	// Post result to UI thread
	PSBRegTestResult *resultData = (PSBRegTestResult*)malloc(sizeof(PSBRegTestResult));
	if (resultData) {
		resultData->success = (result == SUCCESS);
		strncpy(resultData->message, errorMsg, sizeof(resultData->message) - 1);
		resultData->message[sizeof(resultData->message) - 1] = '\0';

		// Show result in cmd_prompt
		if (resultData->success) {
			LogPromptTextbox(CMD_OUTPUT, "PSB Register Matrix Test COMPLETE");
			LogPromptTextbox(CMD_OUTPUT, resultData->message);
		} else {
			LogPromptTextbox(CMD_ERROR, "PSB Register Matrix Test FAILED");
			LogPromptTextbox(CMD_ERROR, resultData->message);
		}

		free(resultData);
	}

	// Release busy flag
	CmtGetLock(g_busyLock);
	g_systemBusy = 0;
	CmtReleaseLock(g_busyLock);

	return 0;
}

static int PSBCommandManager(CommandContext *ctx) {
	char message[1024];

	// Skip leading space (device prefix "PSB" already stripped by DeviceSelect)
	if (ctx->command[0] == ' ') {
		char *command = my_strdup(&ctx->command[1]);
		free(ctx->command);
		ctx->command = command;
	}

	// PSB RTEST - Run register matrix test
	if (strcmp(ctx->command, "RTEST") == 0) {
		// Check if system is busy
		CmtGetLock(g_busyLock);
		if (g_systemBusy) {
			CmtReleaseLock(g_busyLock);
			LogPromptTextbox(CMD_ERROR, "System is busy - cannot start register matrix test");
			return 0;
		}
		CmtReleaseLock(g_busyLock);

		// Check PSB connection
		PSB_Handle *psbHandle = PSB_QueueGetHandle(g_psbQueueMgr);
		if (!psbHandle || !psbHandle->isConnected) {
			LogPromptTextbox(CMD_ERROR, "PSB not connected - cannot run register matrix test");
			return 0;
		}

		LogPromptTextbox(CMD_OUTPUT, "========================================");
		LogPromptTextbox(CMD_OUTPUT, "PSB REGISTER MATRIX TEST");
		LogPromptTextbox(CMD_OUTPUT, "========================================");
		LogPromptTextbox(CMD_OUTPUT, "Duration: ~20 minutes");
		LogPromptTextbox(CMD_OUTPUT, "Total Tests: ~40");
		LogPromptTextbox(CMD_OUTPUT, "");
		LogPromptTextbox(CMD_OUTPUT, "SAFETY FEATURES:");
		LogPromptTextbox(CMD_OUTPUT, "- CC mode limited to MAX 3A (safe)");
		LogPromptTextbox(CMD_OUTPUT, "- CP mode limited to MAX 20W (safe)");
		LogPromptTextbox(CMD_OUTPUT, "- Emergency abort if current > 10A or power > 100W");
		LogPromptTextbox(CMD_OUTPUT, "");
		LogPromptTextbox(CMD_OUTPUT, "Starting test in background thread...");

		// Set system busy
		CmtGetLock(g_busyLock);
		g_systemBusy = 1;
		CmtReleaseLock(g_busyLock);

		// Launch test in background thread
		CmtScheduleThreadPoolFunction(DEFAULT_THREAD_POOL_HANDLE,
		                              PSBRegTestWorkerThread,
		                              NULL, NULL);

		LogPromptTextbox(CMD_OUTPUT, "Test launched - check ops-log for progress");
		return 0;
	}

	// PSB TEST - Quick connection test
	if (strcmp(ctx->command, "TEST") == 0) {
		PSB_Handle *psbHandle = PSB_QueueGetHandle(g_psbQueueMgr);
		if (!psbHandle || !psbHandle->isConnected) {
			LogPromptTextbox(CMD_ERROR, "PSB not connected");
			return 0;
		}

		// Read status to verify communication
		PSB_Status status;
		int error = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_HIGH);

		if (error == SUCCESS) {
			snprintf(message, sizeof(message),
			        "Connection test: OK (Mode: %d, Direction: %d, Output: %s)",
			        status.mode, status.direction, status.outputEnabled ? "ON" : "OFF");
			LogPromptTextbox(CMD_OUTPUT, message);
		} else {
			snprintf(message, sizeof(message), "Connection test failed: %s", GetErrorString(error));
			LogPromptTextbox(CMD_ERROR, message);
		}

		return 0;
	}

	// PSB HELP - Show help
	if (strcmp(ctx->command, "HELP") == 0) {
		LogPromptTextbox(CMD_OUTPUT, "PSB 10000 Commands:");
		LogPromptTextbox(CMD_OUTPUT, "  PSB TEST  - Test PSB connection and communication");
		LogPromptTextbox(CMD_OUTPUT, "  PSB RTEST - Run register matrix test (~20 min, ~40 tests)");
		LogPromptTextbox(CMD_OUTPUT, "  PSB HELP  - Show this help");
		LogPromptTextbox(CMD_OUTPUT, "");
		LogPromptTextbox(CMD_OUTPUT, "RTEST output files:");
		LogPromptTextbox(CMD_OUTPUT, "  - psb_register_matrix_YYYYMMDD_HHMMSS.csv");
		LogPromptTextbox(CMD_OUTPUT, "  - psb_register_summary_YYYYMMDD_HHMMSS.txt");
		return 0;
	}

	// Invalid command
	snprintf(message, sizeof(message), "Invalid PSB command: %s (Use: PSB HELP)", ctx->command);
	LogPromptTextbox(CMD_ERROR, message);
	return 0;
}
