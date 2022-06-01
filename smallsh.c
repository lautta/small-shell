/* Small shell with built-ins
 * August Lautt */

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>

#define MAX_CMD_CHARS 2048
#define MAX_CMD_ARGS 512
#define DELIM " \t\n"
#define DEVNULL "/dev/null"

// struct for command line information
struct Command
{
	// array that holds the commands or arguments
	char *argv[MAX_CMD_ARGS];

	// int that tracks the argument count
	int argc;

	// bool to track if background command given
	int isBgProcess;

	// bool to track if command wants input redirection
	int wantsInputR;

	// bool to track if command wants output redirection
	int wantsOutputR;

	// pointer to filename for input redirection
	char *inRedirFile;

	// pointer to filename for output redirection
	char *outRedirFile;
};


void initCommand(struct Command *cmdInfo);
void freeCommand(struct Command *garbage);
struct Command* getCommand();
int execCommand(struct Command *cmdInfo);
void cleanUp();


// global for easy signal handling
struct sigaction action;

// global string that holds process exit/termination state
char ENDSTATE[MAX_CMD_CHARS] = "NULL";

int main()
{
	// shell loop condition
	int exitCalled = 0;

	struct Command *cmdInfo;

	// set signal handling to prevent signal interuption
	// this will be inherited unless changed later
	action.sa_handler = SIG_IGN;
	sigaction(SIGINT, &action, NULL);

	do
	{
		cleanUp();
		cmdInfo = getCommand();
		exitCalled = execCommand(cmdInfo);
		freeCommand(cmdInfo);
	}while (exitCalled == 0);

	return 0;
}


/* Function that gives Command struct default values.
 * Takes Command struct allocated in memory. */

void initCommand(struct Command *cmdInfo)
{
	memset(cmdInfo->argv, 0, sizeof cmdInfo->argv);

	cmdInfo->argc = 0;
	cmdInfo->isBgProcess = 0;
	cmdInfo->wantsInputR = 0;
	cmdInfo->wantsOutputR = 0;
	cmdInfo->inRedirFile = NULL;
	cmdInfo->outRedirFile = NULL;
}


/* Function that frees memory allocated Command struct.
 * Takes Command struct previously allocated. */

void freeCommand(struct Command *garbage)
{
	free(garbage);
}


/* Function that displays the user prompt, gets the user's input, parses out the
 * commands, arguments, and symbols and fills a Command struct with all the
 * pertinent information to be used when executing or running built-ins.
 * Returns a filled Command struct that has been allocated in memory. */

struct Command* getCommand()
{
	char *token;
	char input[MAX_CMD_CHARS];

	// allocate memory for struct, freed later in main shell loop
	struct Command *newCmd = malloc(sizeof(struct Command));

	// set default values of struct
	initCommand(newCmd);
	memset(input, '\0', sizeof input);

	// print the prompt
	printf(": ");
	fflush(stdout);
	fflush(stdin);

	// get user input
	fgets(input, MAX_CMD_CHARS, stdin);

	// get the first delimited section of the input
	token = strtok(input, DELIM);

	// continue getting token snippets until there are no more
	while (token)
	{
		// if snippet includes input redirect
		if (strcmp(token, "<") == 0)
		{
			// get the next snippet which should be the filename to be used later
			// and set flag and duplicate string into appropriate struct attribute
			// strtok will return NULL if no argument is found
			token = strtok(NULL, DELIM);
			newCmd->wantsInputR = 1;
			newCmd->inRedirFile = strdup(token);
		}
		// if snippet includes output redirect
		else if (strcmp(token, ">") == 0)
		{
			// same as input redirect
			token = strtok(NULL, DELIM);
			newCmd->wantsOutputR = 1;
			newCmd->outRedirFile = strdup(token);
		}
		// if snippet includes background flag, set struct background flag
		else if (strcmp(token, "&") == 0)
		{
			newCmd->isBgProcess = 1;
		}
		// otherwise, add the argument to the arg array and increment count
		else
		{
			newCmd->argv[newCmd->argc++] = strdup(token);
		}

		// get next snippet and continue getting snippets until NULL
		token = strtok(NULL, DELIM);
	}

	// once argument array is full, make sure last argument is NULL for exec
	newCmd->argv[newCmd->argc] = NULL;

	return(newCmd);
}


/* Function to execute commands from the array inside the passed struct. First,
 * check if the built-ins were requested and run their logic, or else fork a
 * process and execute normal linux commands with background/foreground and
 * child/parent logic.
 * Takes a filled Command struct with array containing arguments or commands.
 * Returns bool int of whether to continue shell loop or exiting. */

int execCommand(struct Command *cmdInfo)
{
	// file descriptor and process id for non built-in command use
	int fd;
	pid_t pid;

	// check argument array for built-in commands
	// if blank line, return 0 to continue shell loop
	if (cmdInfo->argv[0] == NULL || cmdInfo->argc == 0)
	{
		return 0;
	}
	// if a comment, return 0 to continue shell loop
	else if (strncmp(cmdInfo->argv[0], "#", 1) == 0)
	{
		return 0;
	}
	// if 'exit', terminate process in process group and return 1 to exit shell loop
	else if (strcmp(cmdInfo->argv[0], "exit") == 0)
	{
		// send terminate signal to current process group
		kill(0, SIGTERM);
		return 1;
	}
	// if 'status', print ENDSTATE then change ENDSTATE to success
	// ENDSTATE is automatically modified when non built-in processes are handled
	else if (strcmp(cmdInfo->argv[0], "status") == 0)
	{
		fprintf(stdout, "%s\n", ENDSTATE);
		fflush(stdout);
		sprintf(ENDSTATE, "exit value 0");
	}
	// if 'cd', change directory to either HOME or supplied argument
	else if (strcmp(cmdInfo->argv[0], "cd") == 0)
	{
		char *directory;

		// no argument given, set to HOME environment
		if (cmdInfo->argv[1] == NULL)
		{
			directory = getenv("HOME");
		}
		else
		{
			directory = cmdInfo->argv[1];
		}

		// change directory to file directory and check for failure
		// and manually edit ENDSTATE because it is a built-in command
		sprintf(ENDSTATE, "exit value 0");

		if (chdir(directory) == -1)
		{
			fprintf(stderr, "no such file or directory\n");
			sprintf(ENDSTATE, "exit value 1");
		}
	}
	// otherwise, the command was not a built-in
	else
	{
		// fork a process
		// child and parent process will both run unless fork fails
		pid = fork();

		// if it is a child process, we will execute command
		if (pid == 0)
		{
			// if not flagged for background, we set signal handling to default
			// in order to interupt the signal
			// background processes will still inherit the SIGINT ignore
			if (cmdInfo->isBgProcess == 0)
			{
				action.sa_handler = SIG_DFL;
				action.sa_flags = 0;
				sigaction(SIGINT, &action, NULL);
			}
			// background process have input/output redirected to /dev/null/,
			// if no file was specified and they want redirection
			else
			{
				if (cmdInfo->inRedirFile == NULL && cmdInfo->wantsInputR == 1)
				{
					cmdInfo->inRedirFile = DEVNULL;
				}

				if (cmdInfo->outRedirFile == NULL && cmdInfo->wantsOutputR == 1)
				{
					cmdInfo->outRedirFile = DEVNULL;
				}
			}

			// if there is input redirect, open file and dup2 to copy stdin fd to the fd
			if (cmdInfo->wantsInputR == 1)
			{
				// open the file in read only
				fd = open(cmdInfo->inRedirFile, O_RDONLY);

				// if there is an error
				// exit with status 1 so our ENDSTATE will be correct
				if (fd == -1)
				{
					fprintf(stderr, "cannot open %s for input\n", cmdInfo->inRedirFile);
					exit(1);
				}

				// otherwise, put stdin file descriptor into the fd
				if (dup2(fd, 0) == -1)
				{
					// check for errors and exit correctly if necessary
					fprintf(stderr, "dup2 error");
					exit(1);
				}

				// close the fd
				close(fd);
			}

			// if there is output redirect, do the same as with input with minor changes
			if (cmdInfo->wantsOutputR == 1)
			{
				// open file and create file if necessary with correct permissions
				fd = open(cmdInfo->outRedirFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

				if (fd == -1)
				{
					fprintf(stderr, "cannot open %s for output\n", cmdInfo->outRedirFile);
					exit(1);
				}

				// this time, put stdout file descriptor into the fd
				if (dup2(fd, 1) == -1)
				{
					fprintf(stderr, "dup2 error");
					exit(1);
				}

				close(fd);
			}

			// execute the command with PATH variable
			if (execvp(cmdInfo->argv[0], cmdInfo->argv) < 0)
			{
				fprintf(stderr, "%s: no such file or directory\n", cmdInfo->argv[0]);
				exit(1);
			}
		}
		// if it is a parent process, handle foreground/background actions
		else if (pid > 0)
		{
			// if it is a foreground process wait for the child to terminate
			if (cmdInfo->isBgProcess == 0)
			{
				int status;

				// since the child process will also run,
				// block parent until specified process ends
				waitpid(pid, &status, 0);

				// grab status/signal with macros depending on which used to end process
				// modify ENDSTATE accordingly
				if (WIFEXITED(status))
				{
					sprintf(ENDSTATE, "exit value %d", WEXITSTATUS(status));
				}
				else if (WIFSIGNALED(status))
				{
					sprintf(ENDSTATE, "terminated by signal %d", WTERMSIG(status));
					// we print this immediately for when signal is terminated
					printf("%s\n", ENDSTATE);
					fflush(stdout);
				}
			}
			// if it is a background process, just print the pid
			else
			{
				printf("background pid is %d\n", pid);
				fflush(stdout);
			}
		}
		// otherwise we had a fork error so exit accordingly
		else
		{
			fprintf(stderr, "fork error");
			exit(1);
		}
	}

	// return 0 to continue the shell loop
	return 0;
}


/* Function to check for any background children processes that have ended and accordingly
 * print the correct information. This will be run at the beginning of the shell loop before
 * the prompt is displayed. */

void cleanUp()
{
	int status;
	pid_t childPid;

	// check if any processes have completed until none are left
	while ((childPid = waitpid(-1, &status, WNOHANG)) > 0)
	{
		// use macros to get correct values and print accordingly
		if (WIFEXITED(status))
		{
			printf("background pid %d is done: exit value %d\n", childPid, WEXITSTATUS(status));
		}
		else if (WIFSIGNALED(status))
		{
			printf("background pid %d is done: terminated by signal %d\n", childPid, WTERMSIG(status));
		}
	}
}
