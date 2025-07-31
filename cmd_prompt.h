/******************************************************************************
 * cmd_prompt.h
 * 
 * Command Prompt Implementation
 * Handles cmd prompt control callbacks and prompt command logic
 ******************************************************************************/

#ifndef CMD_PROMPT_H
#define CMD_PROMPT_H

#define MESSAGE_LENGTH_LIMIT 8
#define OUTPUT_BUFFER_SIZE 1028

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

#endif