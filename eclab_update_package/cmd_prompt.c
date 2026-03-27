/******************************************************************************
 * cmd_prompt.c
 * 
 * Command Prompt Implementation
 * Handles cmd prompt control callbacks and prompt command logic
 ******************************************************************************/

#include "BatteryExploder.h"
#include "common.h"
#include "cmd_prompt.h"
#include "logging.h"
#include "controls.h"
#include "teensy_queue.h"
#include "dtb4848_queue.h"
#include "cdaq_utils.h"
#include "biologic_abstract.h"
#include "biologic_dll.h"

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
	} else if (strncmp(ctx->command, "PID", 3) == 0) {
		// PID commands: PID?, PIDP<val>, PIDI<val>, PIDD<val>, PIDX<val>

		if (strcmp(ctx->command, "PID?") == 0) {
			// Read and display PID parameters
			DTB_PIDParams pidParams;
			int error = DTB_GetPIDParamsQueued(slaveAddress, 0, &pidParams, DEVICE_PRIORITY_HIGH);

			if (error != SUCCESS) {
				char message[1024];
				snprintf(message, 1024, "Failed to read PID parameters: %d : %s", error, GetErrorString(error));
				LogPromptTextbox(CMD_ERROR, message);
				return -1;
			}

			char message[1024];
			snprintf(message, 1024, "PID: P %.1f, I %.0f s, D %.0f s, ID %.1f%%",
			        pidParams.proportionalBand, pidParams.integralTime,
			        pidParams.derivativeTime, pidParams.integralDefault);
			LogPromptTextbox(CMD_OUTPUT, message);

		} else if (ctx->commandLength >= 5 && ctx->command[3] == 'P') {
			// Set proportional band: PIDP<value>
			double value = atof(&ctx->command[4]);

			// Read current parameters
			DTB_PIDParams pidParams;
			int error = DTB_GetPIDParamsQueued(slaveAddress, 0, &pidParams, DEVICE_PRIORITY_HIGH);
			if (error != SUCCESS) {
				LogPromptTextbox(CMD_ERROR, "Failed to read current PID parameters");
				return -1;
			}

			// Update proportional band
			pidParams.proportionalBand = value;
			error = DTB_SetPIDParamsQueued(slaveAddress, 0, &pidParams, DEVICE_PRIORITY_HIGH);

			if (error != SUCCESS) {
				char message[1024];
				snprintf(message, 1024, "Failed to set PID parameters: %d : %s", error, GetErrorString(error));
				LogPromptTextbox(CMD_ERROR, message);
				return -1;
			}

			char message[1024];
			snprintf(message, 1024, "Proportional band set to %.1f", value);
			LogPromptTextbox(CMD_OUTPUT, message);

		} else if (ctx->commandLength >= 5 && ctx->command[3] == 'I') {
			// Set integral time: PIDI<value>
			double value = atof(&ctx->command[4]);

			// Read current parameters
			DTB_PIDParams pidParams;
			int error = DTB_GetPIDParamsQueued(slaveAddress, 0, &pidParams, DEVICE_PRIORITY_HIGH);
			if (error != SUCCESS) {
				LogPromptTextbox(CMD_ERROR, "Failed to read current PID parameters");
				return -1;
			}

			// Update integral time
			pidParams.integralTime = value;
			error = DTB_SetPIDParamsQueued(slaveAddress, 0, &pidParams, DEVICE_PRIORITY_HIGH);

			if (error != SUCCESS) {
				char message[1024];
				snprintf(message, 1024, "Failed to set PID parameters: %d : %s", error, GetErrorString(error));
				LogPromptTextbox(CMD_ERROR, message);
				return -1;
			}

			char message[1024];
			snprintf(message, 1024, "Integral time set to %.0f s", value);
			LogPromptTextbox(CMD_OUTPUT, message);

		} else if (ctx->commandLength >= 5 && ctx->command[3] == 'D') {
			// Set derivative time: PIDD<value>
			double value = atof(&ctx->command[4]);

			// Read current parameters
			DTB_PIDParams pidParams;
			int error = DTB_GetPIDParamsQueued(slaveAddress, 0, &pidParams, DEVICE_PRIORITY_HIGH);
			if (error != SUCCESS) {
				LogPromptTextbox(CMD_ERROR, "Failed to read current PID parameters");
				return -1;
			}

			// Update derivative time
			pidParams.derivativeTime = value;
			error = DTB_SetPIDParamsQueued(slaveAddress, 0, &pidParams, DEVICE_PRIORITY_HIGH);

			if (error != SUCCESS) {
				char message[1024];
				snprintf(message, 1024, "Failed to set PID parameters: %d : %s", error, GetErrorString(error));
				LogPromptTextbox(CMD_ERROR, message);
				return -1;
			}

			char message[1024];
			snprintf(message, 1024, "Derivative time set to %.0f s", value);
			LogPromptTextbox(CMD_OUTPUT, message);

		} else if (ctx->commandLength >= 5 && ctx->command[3] == 'X') {
			// Set integral default: PIDX<value>
			double value = atof(&ctx->command[4]);

			// Read current parameters
			DTB_PIDParams pidParams;
			int error = DTB_GetPIDParamsQueued(slaveAddress, 0, &pidParams, DEVICE_PRIORITY_HIGH);
			if (error != SUCCESS) {
				LogPromptTextbox(CMD_ERROR, "Failed to read current PID parameters");
				return -1;
			}

			// Update integral default
			pidParams.integralDefault = value;
			error = DTB_SetPIDParamsQueued(slaveAddress, 0, &pidParams, DEVICE_PRIORITY_HIGH);

			if (error != SUCCESS) {
				char message[1024];
				snprintf(message, 1024, "Failed to set PID parameters: %d : %s", error, GetErrorString(error));
				LogPromptTextbox(CMD_ERROR, message);
				return -1;
			}

			char message[1024];
			snprintf(message, 1024, "Integral default set to %.1f%%", value);
			LogPromptTextbox(CMD_OUTPUT, message);

		} else {
			LogPromptTextbox(CMD_ERROR, "Invalid PID command. Use: PID?, PIDP<val>, PIDI<val>, PIDD<val>, or PIDX<val>");
		}
	} else if (strncmp(ctx->command, "HCP", 3) == 0) {
		// HCP commands: HCP?, HCP1<val>, HCP2<val> (Heating Control Period / cycle time)

		if (strcmp(ctx->command, "HCP?") == 0) {
			// Read and display control cycle times for both outputs
			int cycle1, cycle2;
			int error1 = DTB_GetControlCycleQueued(slaveAddress, 1, &cycle1, DEVICE_PRIORITY_HIGH);
			int error2 = DTB_GetControlCycleQueued(slaveAddress, 2, &cycle2, DEVICE_PRIORITY_HIGH);

			if (error1 != SUCCESS) {
				char message[1024];
				snprintf(message, 1024, "Failed to read output 1 control cycle: %d : %s", error1, GetErrorString(error1));
				LogPromptTextbox(CMD_ERROR, message);
				return -1;
			}

			if (error2 != SUCCESS) {
				char message[1024];
				snprintf(message, 1024, "Failed to read output 2 control cycle: %d : %s", error2, GetErrorString(error2));
				LogPromptTextbox(CMD_ERROR, message);
				return -1;
			}

			char message[1024];
			const char *time1Str = (cycle1 == 0) ? "0.5" : "";
			const char *time2Str = (cycle2 == 0) ? "0.5" : "";
			if (cycle1 == 0 && cycle2 == 0) {
				snprintf(message, 1024, "Control Cycles: OUT1=0.5s, OUT2=0.5s");
			} else if (cycle1 == 0) {
				snprintf(message, 1024, "Control Cycles: OUT1=0.5s, OUT2=%ds", cycle2);
			} else if (cycle2 == 0) {
				snprintf(message, 1024, "Control Cycles: OUT1=%ds, OUT2=0.5s", cycle1);
			} else {
				snprintf(message, 1024, "Control Cycles: OUT1=%ds, OUT2=%ds", cycle1, cycle2);
			}
			LogPromptTextbox(CMD_OUTPUT, message);

		} else if (ctx->commandLength >= 5 && ctx->command[3] == '1') {
			// Set output 1 control cycle: HCP1<value>
			int value = atoi(&ctx->command[4]);

			if (value < 0 || value > 99) {
				LogPromptTextbox(CMD_ERROR, "Control cycle must be 0-99 (0 = 0.5 sec)");
				return -1;
			}

			int error = DTB_SetControlCycleQueued(slaveAddress, 1, value, DEVICE_PRIORITY_HIGH);

			if (error != SUCCESS) {
				char message[1024];
				snprintf(message, 1024, "Failed to set control cycle: %d : %s", error, GetErrorString(error));
				LogPromptTextbox(CMD_ERROR, message);
				return -1;
			}

			char message[1024];
			if (value == 0) {
				snprintf(message, 1024, "Output 1 control cycle set to 0.5 seconds");
			} else {
				snprintf(message, 1024, "Output 1 control cycle set to %d seconds", value);
			}
			LogPromptTextbox(CMD_OUTPUT, message);

		} else if (ctx->commandLength >= 5 && ctx->command[3] == '2') {
			// Set output 2 control cycle: HCP2<value>
			int value = atoi(&ctx->command[4]);

			if (value < 0 || value > 99) {
				LogPromptTextbox(CMD_ERROR, "Control cycle must be 0-99 (0 = 0.5 sec)");
				return -1;
			}

			int error = DTB_SetControlCycleQueued(slaveAddress, 2, value, DEVICE_PRIORITY_HIGH);

			if (error != SUCCESS) {
				char message[1024];
				snprintf(message, 1024, "Failed to set control cycle: %d : %s", error, GetErrorString(error));
				LogPromptTextbox(CMD_ERROR, message);
				return -1;
			}

			char message[1024];
			if (value == 0) {
				snprintf(message, 1024, "Output 2 control cycle set to 0.5 seconds");
			} else {
				snprintf(message, 1024, "Output 2 control cycle set to %d seconds", value);
			}
			LogPromptTextbox(CMD_OUTPUT, message);

		} else {
			LogPromptTextbox(CMD_ERROR, "Invalid HCP command. Use: HCP?, HCP1<val>, or HCP2<val> (0-99, 0=0.5s)");
		}
	} else if (strncmp(ctx->command, "OUT", 3) == 0) {
		// OUT commands: OUT? (read output percentages for both outputs)

		if (strcmp(ctx->command, "OUT?") == 0) {
			// Read and display output percentages for both outputs
			double output1, output2;
			int error1 = DTB_GetOutputValueQueued(slaveAddress, 1, &output1, DEVICE_PRIORITY_HIGH);
			int error2 = DTB_GetOutputValueQueued(slaveAddress, 2, &output2, DEVICE_PRIORITY_HIGH);

			if (error1 != SUCCESS) {
				char message[1024];
				snprintf(message, 1024, "Failed to read output 1 value: %d : %s", error1, GetErrorString(error1));
				LogPromptTextbox(CMD_ERROR, message);
				return -1;
			}

			if (error2 != SUCCESS) {
				char message[1024];
				snprintf(message, 1024, "Failed to read output 2 value: %d : %s", error2, GetErrorString(error2));
				LogPromptTextbox(CMD_ERROR, message);
				return -1;
			}

			char message[1024];
			snprintf(message, 1024, "Output Values: OUT1=%.1f%%, OUT2=%.1f%%", output1, output2);
			LogPromptTextbox(CMD_OUTPUT, message);

		} else {
			LogPromptTextbox(CMD_ERROR, "Invalid OUT command. Use: OUT? to read output percentages");
		}
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
	char message[1024];
	int error;

	// DAQALL - Read all 16 current channels
	if (strcmp(ctx->command, "ALL") == 0) {
		double currents[CDAQ_CHANNELS_PER_SLOT];
		int num_read;

		error = CDAQ_ReadCurrentArray(currents, &num_read);
		if (error != SUCCESS) {
			snprintf(message, sizeof(message), "Failed to read current array: %d : %s",
			        error, GetErrorString(error));
			LogPromptTextbox(CMD_ERROR, message);
			return -1;
		}

		// Output all channels in a compact format
		char output[1024];
		int pos = 0;
		for (int i = 0; i < num_read; i++) {
			const char *status;
			if (currents[i] < 3.5) {
				status = "FAULT";
			} else if (currents[i] > 20.5) {
				status = "OVER";
			} else if (currents[i] < 4.5) {
				status = "LOW";
			} else if (currents[i] > 19.5) {
				status = "HIGH";
			} else {
				status = "OK";
			}

			pos += snprintf(output + pos, sizeof(output) - pos,
			               "CH%d: %.2f mA (%s)  ", i, currents[i], status);

			// Output every 4 channels on a separate line
			if ((i + 1) % 4 == 0 || i == num_read - 1) {
				LogPromptTextbox(CMD_OUTPUT, output);
				pos = 0;
			}
		}

		return 0;
	}

	// DAQV<channel> - Read voltage (diagnostic mode)
	if (ctx->command[0] == 'V' && ctx->commandLength > 1) {
		int channel = atoi(&ctx->command[1]);

		if (channel < 0 || channel >= CDAQ_CHANNELS_PER_SLOT) {
			snprintf(message, sizeof(message), "Invalid channel %d (must be 0-15)", channel);
			LogPromptTextbox(CMD_ERROR, message);
			return -1;
		}

		double voltage;
		error = CDAQ_ReadVoltage(channel, &voltage);
		if (error != SUCCESS) {
			snprintf(message, sizeof(message), "Failed to read voltage: %d : %s",
			        error, GetErrorString(error));
			LogPromptTextbox(CMD_ERROR, message);
			return -1;
		}

		snprintf(message, sizeof(message), "Channel %d: %.4f V", channel, voltage);
		LogPromptTextbox(CMD_OUTPUT, message);

		return 0;
	}

	// DAQ<channel> - Read single channel current
	if (isdigit(ctx->command[0])) {
		int channel = atoi(ctx->command);

		if (channel < 0 || channel >= CDAQ_CHANNELS_PER_SLOT) {
			snprintf(message, sizeof(message), "Invalid channel %d (must be 0-15)", channel);
			LogPromptTextbox(CMD_ERROR, message);
			return -1;
		}

		double current_mA;
		error = CDAQ_ReadCurrent(channel, &current_mA);
		if (error != SUCCESS) {
			snprintf(message, sizeof(message), "Failed to read current: %d : %s",
			        error, GetErrorString(error));
			LogPromptTextbox(CMD_ERROR, message);
			return -1;
		}

		// Determine status
		const char *status;
		if (current_mA < 3.5) {
			status = "FAULT";
		} else if (current_mA > 20.5) {
			status = "OVER-RANGE";
		} else if (current_mA < 4.5) {
			status = "LOW";
		} else if (current_mA > 19.5) {
			status = "HIGH";
		} else {
			status = "OK";
		}

		snprintf(message, sizeof(message), "Channel %d: %.3f mA (%s)",
		        channel, current_mA, status);
		LogPromptTextbox(CMD_OUTPUT, message);

		return 0;
	}

	// Invalid command
	LogPromptTextbox(CMD_ERROR, "Invalid DAQ command. Use: DAQ<0-15>, DAQALL, or DAQV<0-15>");
	return 0;
}

static int BioLogicCommandManager(CommandContext *ctx) {
	char message[1024];
	int error;

	// Trim leading whitespace from command
	char *trimmed = TrimWhitespace(ctx->command);
	char *command = my_strdup(trimmed);
	free(ctx->command);
	ctx->command = command;

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
			snprintf(message, sizeof(message), "Connection test failed: %d : %s",
			        error, GetErrorString(error));
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
		LogPromptTextbox(CMD_OUTPUT, "Running OCV measurement...");

		BIO_TechniqueData *result = NULL;
		error = BIO_Abstract_RunOCV(0,          // channel 0
		                           10.0,       // 10 second duration
		                           1.0,        // 1 second interval
		                           0.0,        // no dE threshold
		                           0.0,        // no dT threshold
		                           0,          // auto E range
		                           &result,
		                           OCV_TIMEOUT_MS,  // Use timeout from common.h
		                           NULL,       // no progress callback
		                           NULL,       // no user data
		                           NULL);      // no cancel flag

		if (error != SUCCESS) {
			snprintf(message, sizeof(message), "OCV failed: %d : %s",
			        error, GetErrorString(error));
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

	// BIO PEIS - Run quick PEIS test
	if (strcmp(ctx->command, "PEIS") == 0) {
		LogPromptTextbox(CMD_OUTPUT, "Running PEIS measurement (10kHz-0.1Hz, 10mV amplitude)...");

		BIO_TechniqueData *result = NULL;
		error = BIO_Abstract_RunPEIS(
			0,                   // channel 0
			true,                // vs_initial
			0.0,                 // initial_voltage_step (V)
			1.0,                 // duration_step (s)
			0.0,                 // record_every_dT (s)
			0.0,                 // record_every_dI (A)
			10000.0,             // initial_freq: 10 kHz
			0.1,                 // final_freq: 0.1 Hz
			false,               // sweep_linear (logarithmic)
			0.010,               // amplitude_voltage: 10 mV
			11,                  // frequency_number (5 decades)
			2,                   // average_n_times
			false,               // correction
			0.1,                 // wait_for_steady (periods)
			&result,
			100000,              // timeout_ms (100 seconds)
			NULL,                // no progress callback
			NULL,                // no user data
			NULL);               // no cancel flag

		if (error != SUCCESS) {
			snprintf(message, sizeof(message), "PEIS failed: %d : %s",
			        error, GetErrorString(error));
			LogPromptTextbox(CMD_ERROR, message);
			return -1;
		}

		if (result && result->rawData && result->rawData->numPoints > 0) {
			snprintf(message, sizeof(message), "PEIS complete: %d frequency points",
			        result->rawData->numPoints);
			LogPromptTextbox(CMD_OUTPUT, message);
			BIO_FreeTechniqueData(result);
		} else {
			LogPromptTextbox(CMD_ERROR, "PEIS returned no data");
		}

		return 0;
	}

	// BIO GEIS - Run quick GEIS test
	if (strcmp(ctx->command, "GEIS") == 0) {
		LogPromptTextbox(CMD_OUTPUT, "Running GEIS measurement (10kHz-0.1Hz, 500mA amplitude)...");

		BIO_TechniqueData *result = NULL;
		error = BIO_Abstract_RunGEIS(
			0,                   // channel 0
			GEIS_VS_INITIAL,     // vs_initial
			GEIS_INITIAL_CURRENT,// initial_current_step (A)
			GEIS_DURATION_S,     // duration_step (s)
			GEIS_RECORD_EVERY_DT,// record_every_dT (s)
			GEIS_RECORD_EVERY_DE,// record_every_dE (V)
			GEIS_INITIAL_FREQ,   // initial_freq: 10 kHz
			GEIS_FINAL_FREQ,     // final_freq: 0.1 Hz
			GEIS_SWEEP_LINEAR,   // sweep_linear (logarithmic)
			GEIS_AMPLITUDE_I,    // amplitude_current: 500 mA
			GEIS_FREQ_NUMBER,    // frequency_number
			GEIS_AVERAGE_N,      // average_n_times
			GEIS_CORRECTION,     // correction
			GEIS_WAIT_FOR_STEADY,// wait_for_steady (periods)
			GEIS_I_RANGE,        // i_range (1A range)
			&result,
			GEIS_TIMEOUT_MS,     // timeout_ms
			NULL,                // no progress callback
			NULL,                // no user data
			NULL);               // no cancel flag

		if (error != SUCCESS) {
			snprintf(message, sizeof(message), "GEIS failed: %d : %s",
			        error, GetErrorString(error));
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

	// BIO HELP - Show help
	if (strcmp(ctx->command, "HELP") == 0) {
		LogPromptTextbox(CMD_OUTPUT, "BioLogic Abstraction Commands:");
		LogPromptTextbox(CMD_OUTPUT, "  BIO MODE               - Show current control mode (DLL/EC-Lab)");
		LogPromptTextbox(CMD_OUTPUT, "  BIO TEST               - Test connection to device");
		LogPromptTextbox(CMD_OUTPUT, "  BIO ID                 - Get device ID");
		LogPromptTextbox(CMD_OUTPUT, "  BIO OCV                - Run quick 10s OCV test");
		LogPromptTextbox(CMD_OUTPUT, "  BIO PEIS               - Run quick PEIS test (10kHz-0.1Hz, 10mV)");
		LogPromptTextbox(CMD_OUTPUT, "  BIO GEIS               - Run quick GEIS test (10kHz-0.1Hz, 500mA)");
		LogPromptTextbox(CMD_OUTPUT, "  BIO HELP               - Show this help");
		LogPromptTextbox(CMD_OUTPUT, "");
		LogPromptTextbox(CMD_OUTPUT, "Note: Uses abstraction layer (auto DLL/EC-Lab mode)");
		return 0;
	}

	// Invalid command
	snprintf(message, sizeof(message), "Invalid BIO command: %s (Use: BIO HELP)", ctx->command);
	LogPromptTextbox(CMD_ERROR, message);
	return 0;
}
