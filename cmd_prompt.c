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
#include "teensy_queue.h"

typedef struct {
	enum Status {
		CMD_ERROR = 0,
		CMD_INPUT,
		CMD_OUTPUT
	} status;
	char *message;
} UIUpdateData;

typedef struct {
	char *command;
	int commandLength;
} CommandContext;

static int CmdPromptSendThread();
static void LogPromptTextbox(enum Status status, char *message);
static void DeferredPromptTextboxUpdate(void *data);
static int DeviceSelect(CommandContext *ctx);
static int TeensyCommandManager(CommandContext *ctx);

int CVICALLBACK CmdPromptSendCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2){
	if (event != EVENT_COMMIT) {
		return 0;
	}
	
	int threadID;
	CmtScheduleThreadPoolFunction(g_threadPool, CmdPromptSendThread, NULL, &threadID);
	
	if (threadID == 0) {
		LogPromptTextbox(CMD_ERROR, "There was an error scheduling the command send thread.");
		return -1;
	}
	
	return 0;
}

static void LogPromptTextbox(enum Status status, char *message) {
	UIUpdateData *data = malloc(sizeof(UIUpdateData));
	data->status = status;
	data->message = my_strdup(message);
	PostDeferredCall(DeferredPromptTextboxUpdate, data);
}

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

static int CmdPromptSendThread() {
	CommandContext *ctx = malloc(sizeof(CommandContext));
	memset(ctx, 0, sizeof(CommandContext));
	
	int commandLength;
	GetCtrlAttribute(g_mainPanelHandle, PANEL_STR_CMD_PROMPT_INPUT, ATTR_STRING_TEXT_LENGTH, &commandLength);
	
	char *raw = malloc(commandLength + 1);
	GetCtrlVal(g_mainPanelHandle, PANEL_STR_CMD_PROMPT_INPUT, raw);
	char *trimmed = TrimWhitespace(raw);
	ctx->command = my_strdup(trimmed);
	free(raw);
	
	SetCtrlVal(g_mainPanelHandle, PANEL_STR_CMD_PROMPT_INPUT, "");
	
	commandLength = strlen(ctx->command);
	if (commandLength < 4) {
		goto cleanup;
	} else if (commandLength > MESSAGE_LENGTH_LIMIT) {
		LogPromptTextbox(CMD_ERROR, "Message Length Error: The command you've entered is too long.");
		goto cleanup;
	}
	
	LogPromptTextbox(CMD_INPUT, ctx->command);
	
	DeviceSelect(ctx);
	
cleanup:
	free(ctx->command);
	free(ctx);
	
	return 0;		
}

static int DeviceSelect(CommandContext *ctx) {
	int deviceCode = (ctx->command[0] << 16 | ctx->command[1] << 8 | ctx->command[2]);
	
	// Remove first three characters specifying device
	char *trimmed = malloc(ctx->commandLength - 2);	// Trim 3 characters but account for the \0 null termination character
	trimmed = my_strdup(&ctx->command[3]);
	free(ctx->command);
	ctx->command = trimmed;
	ctx->commandLength -= 3;
	
	switch (deviceCode) {
		case ('T' << 16 | 'N' << 8 | 'Y'):
			TeensyCommandManager(ctx);
			break;
			
		default:
			LogPromptTextbox(CMD_ERROR, "No such device.");
	}
	
	return 0;
}

int TeensyCommandManager(CommandContext *ctx) {
	if (strlen(ctx->command) != 4) {
		LogPromptTextbox(CMD_ERROR, "Teensy serial commands must be exactly 4 characters.");
		return 0;
	}
	
	int error;
	char response[16];
	error = TNY_SendRawCommandQueued(NULL, ctx->command, response, 16);
	
	if (error != SUCCESS) {
		char message[1024];
		snprintf(message, 1024, "Raw command failed: %d : %s", error, GetErrorString(error));
		LogPromptTextbox(CMD_ERROR, message);
		return -1;
	}
	
	LogPromptTextbox(CMD_OUTPUT, response);
	
	return 0;
}
							