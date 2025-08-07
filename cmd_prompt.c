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
	error = TNY_SendRawCommandQueued(ctx->command, response, 16);
	
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
		int error = DTB_FactoryResetQueued(slaveAddress);
		
		if (error != SUCCESS) {
			char message[1024];
			snprintf(message, 1024, "Reset command failed: %d : %s", error, GetErrorString(error));
			LogPromptTextbox(CMD_ERROR, message);
			return -1;
		}
		
		LogPromptTextbox(CMD_OUTPUT, "Reset command success.");
	} else if (strcmp(ctx->command, "SETUP") == 0) {
		int error = DTB_EnableWriteAccessQueued(slaveAddress);
		
		if (error != SUCCESS) {
			char message[1024];
			snprintf(message, 1024, "Write access command failed: %d : %s", error, GetErrorString(error));
			LogPromptTextbox(CMD_ERROR, message);
			return -1;
		}
		
		error = DTB_ConfigureDefaultQueued(slaveAddress);
		
		if (error != SUCCESS) {
			char message[1024];
			snprintf(message, 1024, "Configure command failed: %d : %s", error, GetErrorString(error));
			LogPromptTextbox(CMD_ERROR, message);
			return -1;
		}
		
		LogPromptTextbox(CMD_OUTPUT, "Setup command success.");
	} else if (strcmp(ctx->command, "AT") == 0) {
		int error = DTB_StartAutoTuningQueued(slaveAddress);
		
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
							