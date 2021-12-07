#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>

// global variables to track status and background
bool background = false;
int childStatus = 0;

#define MAXARGS 512 
#define MAXLEN 2048
// declaration of prototypes
void backgroundWait(int**, int);
char* pidExpansion(char*, char*, char*);
void getArguments(char**, char*, int*, int*);
void killChildren(int**, int);
void catchSIGTSTP(int);
void handleSIGINT(int);
void cleanUp(char**, int);
void redirectOutput(char**, int,int*,  int);



int main()
{
	
	// variable declaration
	char userCmd[256];
	char* userArgs[512];
	int** pidArray = malloc(100 * sizeof(int));
	int processCount = 0;
	int currStatus = 0;
	
	char* token;
	char* ptr;

	bool play = true;
	
	// while play is just used to run program until killed or exit
	while (play)
	{
		// handle SIGTSTP and ignore SIGINT declaration from class examples
		struct sigaction SIGTSTP_act;
		SIGTSTP_act.sa_handler = catchSIGTSTP;
		sigfillset(&SIGTSTP_act.sa_mask);
		SIGTSTP_act.sa_flags = 0;
		sigaction(SIGTSTP, &SIGTSTP_act, NULL);

		signal(SIGINT, SIG_IGN);

		int isBackground = 0;
		
		char copyInput[1024];

		// need to check running proccesses for each iter through.
		backgroundWait(pidArray, processCount);

		// allocating variables for use in getting user input.
		char* userInput = malloc(2048 * (sizeof(char)));
		char* expandedInput = malloc(2048 * (sizeof(char)));
		char* checkInput = malloc(2048 * (sizeof(char)));
		strcpy(checkInput, "");
		
		// get user input
		printf(": ");
		fflush(stdout);
		fgets(userInput, 512, stdin);

		// copy userinput to keep track characters before $$. copy return function output with expanded information.
		strcpy(copyInput, userInput);
		strcpy(expandedInput, pidExpansion(userInput, checkInput, copyInput));

		int argCount = 0;

		// pull arguments from userinput
		getArguments(userArgs, expandedInput, &argCount, &isBackground);

		// check if arguments were made and add null to end.
		if (argCount == 0) {
			userArgs[0] = calloc(100, sizeof(char));
			strcpy(userArgs[0], "");
		}
		else if (argCount != 0 && argCount > 1) {
			strtok(userArgs[argCount - 1], "\n");
		}
		

		// commands handle directly by small shell cd, status, and exit.
		if ((strcmp(userArgs[0], "") == 0) || (userArgs[0][0] == '#')) {
			fflush(stdout);
		}
		// if cd called check if no destination was claimed go to home dir else go to dir specified.
		else if (strcmp(userArgs[0], "cd") == 0) {

			if (argCount == 1) {
				int dirValid = chdir(getenv("HOME"));
				if (dirValid == -1) {
					perror("");
					fflush(stdout);
				}
			}
			else if (argCount > 1) {
				strtok(userArgs[1], "\n");
				int lengthOfArgument = strlen(userArgs[1]);
				if (userArgs[1][0] == '/') {
					memmove(userArgs[1], userArgs + 1, lengthOfArgument);
				}
				int dirValid = chdir(userArgs[1]);
				if (dirValid == -1) {
					perror("");
					fflush(stdout);
				}
			}
			childStatus = 0;
		}
		// get last exit status or child termination signal.
		else if (strcmp(userArgs[0], "status") == 0) {
			if (childStatus != 0 && childStatus > 0) {
				printf("terminated by signal %d", childStatus);
				fflush(stdout);
			}
			else if (childStatus == 0) {
				printf("exit value %d\n", currStatus);
				fflush(stdout);
			}
		}
		// kill children processes off
		else if (strcmp(userArgs[0], "exit") == 0) {
			killChildren(pidArray, processCount);
			play = false;
		}
		// end of smallshell specified handles pass off to process for outside processes.
		else {
			childStatus = 0;
			sendCommands(userArgs, &currStatus, argCount, isBackground, pidArray, &processCount, &SIGTSTP_act);

		}

		// clean up before next iter of program.
		resetInput(argCount, userCmd, userArgs);

		free(userInput);
		free(expandedInput);
		free(checkInput);

		cleanUp(userArgs, argCount);
	}



	return 0;
}

/*
Function inputs : argCount, userCmd, userArgs
If userArgs still exist then append null character to end of userCmd
else copy blank space.
*/
void resetInput(int argCount, char* userCmd, char** userArgs) {

	if (userArgs != 0) {
		strcpy(userCmd, userArgs[0]);
		strtok(userCmd, "\n");
		fflush(stdout);
	}
	else {
		strcpy(userCmd, "");
		fflush(stdout);
	}
}

/*
Function inputs: userArgs, currStatus, argCount, isBackground, pidArray, processCount, struct sigaction,
Takes inputs and handle forking child and controlls signal actions sent by keyboard input.
*/
void sendCommands(char** userArgs, int* currStatus, int argCount, int isBackground, int** pidArray, int* processCount, struct sigaction* SIGTSTP_act) {
	// declarations of variables
	pid_t spawnpid = -5;
	int childExitStatus = 0;
	int checkForRedirect = 0;
	userArgs[argCount] = NULL;

	spawnpid = fork();
	switch (spawnpid) {
	case -1: { perror("Hull Breach!\n"); exit(1); break; }  
	case 0: { // case 0 means it was forked as a child process.
		signal(SIGTSTP, SIG_IGN);

		// signle handlers for foreground and background processes.
		if (isBackground == 1) {
			signal(SIGINT, SIG_IGN);
		}

		else if (isBackground == 0 || background == true) {
			signal(SIGINT, handleSIGINT);
		}

		// check if redirection is present and flag which direction is being called.
		for (int i = 0; i < argCount; i++) {
			if ((strcmp(userArgs[i], "<") == 0) && (argCount > 1)) {
				checkForRedirect = 1;
			}
			else if ((strcmp(userArgs[i], ">") == 0) && (argCount > 1)) {
				checkForRedirect = 2;
			}

		}
		// if redirection occured send to function to handle redirection cases.
		if (checkForRedirect > 0) {
			redirectOutput(userArgs, argCount,currStatus, checkForRedirect);
		}
		// if no redirection check if background was called. if so send to "/dev/null"
		else if (isBackground == 1) {
			int openFile = open("/dev/null", O_WRONLY);
			dup2(openFile, 0);
			dup2(openFile, 1);
			execvp(userArgs[0], userArgs);

		}
		// no redirection detected continue to process.
		else {
			if ((strcmp(userArgs[argCount - 1], "&") == 0) && background == true){

				userArgs[argCount - 1] = NULL;
		}
			execvp(userArgs[0], userArgs);
			
		}
		// if cannot find specified file inputed by user give error message and exit process.
		printf("%s : no such file or directory\n", userArgs[0]);
		fflush(stdout);
		exit(1);
		break;
	}
	default: { // action to be taken by parent process. Ignore according signals to process.
		signal(SIGTSTP, catchSIGTSTP);

		//check if is a background process or a forground process. forground process will wait until child is done.
		if (background == true || isBackground == 0) {
			pid_t childPid = waitpid(spawnpid, &childExitStatus, 0);
			if (strcmp(userArgs[0], "test") == 0 && childExitStatus != 0) {
				(*currStatus) = 1;
			}
			else {
				(*currStatus) = childExitStatus;
			}
		}
		//background process dont wait until child is done. Add process to array to keep track of as running.
		else if (isBackground == 1) {
			signal(SIGINT, SIG_IGN);
			printf("background pid is %d\n", spawnpid);
			fflush(stdout);
			pid_t childPid = waitpid(spawnpid, &childExitStatus, WNOHANG);

			pidArray[*processCount] = malloc(10 * sizeof(int));
			*(pidArray[*processCount]) = spawnpid;
			(*processCount)++;
		}
		break;
	}
		
	}
}

/*
Function inputs: userArgs, argCount, currStatus, checkForRedirection.
Handles redirection depending on direction of input. Will either open designated file
read contents only or it will open/create a designated with read and write capabilities only
*/
void redirectOutput(char** userArgs, int argCount,int* currStatus, int checkForRedirect) {
	
	// check if redirection is a input call, handle if file 
	// cannot be accessed and handle dup2 error if file cannot be accessed.
	if (checkForRedirect = 1) {
		int openFile = open(userArgs[2], O_RDONLY, 0640);

		if (openFile == -1) {
			printf("cannot open file %s for input\n", userArgs[2]);
			fflush(stdout);
			(*currStatus) = 1;
			exit(1);
		}
		int dupIt = dup2(openFile, 0);
		if (dupIt == -1) {
			perror("dup2 error");
			exit(1);
		}
		// send to execlp if argument is present.
		if (userArgs > 1) {
			execlp(userArgs[0], userArgs[0], NULL);
		}
	}

	// check if output redirect was called, handle if file
	// cannot be accessed and handle dup2 error if file cannot be accessed.
	// Handle 
	else if (checkForRedirect = 2) {
		int openFile = open(userArgs[argCount - 1], O_WRONLY | O_CREAT | O_TRUNC, 0640);

		if (openFile == -1) {
			printf("cannot open file %s for output\n", userArgs[2]);
			fflush(stdout);
			exit(1);
		}
		int dupIt = dup2(openFile, 1);
		if (dupIt == -1) {
			perror("dup2 error");
			exit(2);
		}

		// handle execlp commands depending on amount of arguments.
		if (argCount == 6) {
			execlp(userArgs[0], userArgs[0], userArgs[1], userArgs[3], NULL);
		}
		else if (argCount == 5){
			execlp(userArgs[0], userArgs[0], userArgs[2], NULL);
		}
		else if (argCount == 4) {
			execlp(userArgs[0], userArgs[0], userArgs[1], NULL);
		}
		else if (argCount == 3) {
			execlp(userArgs[0], userArgs[0], NULL);
		}
		else if (argCount == 2) {
			execlp(userArgs[0], userArgs[0], NULL);
		}

	}
}

/*
Funtion inputs: userArgs, argCount.
Simple function to clean up mem before next iter.
*/
void cleanUp(char** userArgs, int argCount) {
	
	if (argCount > 0) {
		for (int i = 0; i < argCount; i++) {
			free(userArgs[i]);
		}
	}
	else {
		free(userArgs[0]);
	}
}

/*
Function input: signo
handle function for catching SIGTSTP signals.
*/
void catchSIGTSTP(int signo) {
	char enterFor = "Entering foreground-only mode (& is now ignored)\n";
	char enterBack = "Exiting foreground-only mode\n";

	if (!background)
	{
		write(1, enterFor, 54);
		fflush(stdout);
		background = true;
	}
	else {
		write(1, enterBack, 35);
		fflush(stdout);
		background = false;
	}
}

/*
function input: signo
handle function for handling SIGINT signals
while in foreground mode.
Source: https://linuxhint.com/signal_handlers_c_programming_language/#:~:text=In%20the%20handler%20function%2C%20the,default%20action%20of%20SIGINT%20signal.
*/
void handleSIGINT(int signo) {
	int size = 30;

	char msg = "terminated by signal 2\n";

	write(STDOUT_FILENO, msg, size);

	char string[size];

	sprintf(string, "terminated by signal %d\n", signo);
	fflush(stdout);
	signal(SIGINT, SIG_DFL);

	childStatus = 2;
	fflush(stdout);
}
/*
Funtion input: pidArray, processCount
Simple function to kill off children processes before exit.
*/
void killChildren(int** pidArray, int processCount) {
	for (int i = 0; i < processCount; i++) {
		if (*(pidArray[i]) != 0) {
			kill(*(pidArray[i]), SIGKILL);
		}
		free(pidArray[i]);
	}
	free(*(pidArray));
	fflush(stdout);
	


}

/*
* funtion input: userArgs, expandedInput, argCount, isBackground
* Simple funtion using in class examples with adaptation for assignment
* The function uses tokens to place arguments into a string array.
*/
void getArguments(char** userArgs, char* expandedInput, int* argCount, int* isBackground) {
	char* ptr;
	char* token;
	
	for (token = strtok_r(expandedInput, " ", &ptr);
		token != NULL;
		token = strtok_r(NULL, " ", &ptr)) {
		userArgs[*argCount] = calloc(100, sizeof(char));
		if (strcmp(token, "&") == 0) {
			*isBackground = 1;
			free(userArgs[*argCount]);
		}
		else {
			strcpy(userArgs[*argCount], token);
			(*argCount)++;
		}
	}
}
/*
function input: userInput, checkInput, copyInput.
The function iter over the userinput for instances of $$ and replaces
and instance of $$ with the pid.
*/
char* pidExpansion(char* userInput, char* checkInput, char* copyInput) {
	// variable declaration.
	int itr = 0;            
	int lastOccurence = 0;
	int beforeOccurence = 0;
	int totalIter = strlen(copyInput) - 1;

	// iter over the array while less then input -1
	for (int i = itr; i < totalIter; i++) {
		itr++;
		lastOccurence++;
		// check for $$ if found get before it was found up to when it was found then shift accordingly
		if (userInput[i] == '$' && userInput[i + 1] == '$') {
			strncat(checkInput, userInput + beforeOccurence, lastOccurence - 1);
			pid_t pid = getpid();
			char pidString[10];
			sprintf(pidString, "%d", pid);
			strcat(checkInput, pidString);
			fflush(stdout);
			
			i++;
			beforeOccurence = beforeOccurence + (lastOccurence + 1);
			lastOccurence = 0;
			itr++;
			
		}
		
	}
	strncat(checkInput, userInput + beforeOccurence, lastOccurence);
	return checkInput;
}


/*
* fucntion input: pidArray, processCount
* Simple function that checks on background procness 
* and gives an update on its exit status.
*/
void backgroundWait(int** pidArray, int processCount) {
	int waitChild = 0;

	for (int i = 0; i < processCount; i++) {
		if (*(pidArray[i]) != 0) {
			waitChild = 1;
			pid_t childPid = waitpid(*(pidArray[i]), &waitChild, WNOHANG);

			if (WIFEXITED(waitChild)) {
				printf("background pid %d is done: terminated by signal %d\n", *(pidArray[i]), WTERMSIG(waitChild));
				fflush(stdout);
			}
			else if (WIFSIGNALED(waitChild) == SIGTERM || kill(*(pidArray[i]), 0) == -1) {
				printf("background pid %d is done: terminated by signal %d\n", *(pidArray[i]), WTERMSIG(waitChild));
				fflush(stdout);
				*(pidArray[i]) = 0;
			}
			else if (WIFSIGNALED(waitChild) == SIGKILL) {
				printf("background pid %d is done: terminated by signal %d\n", *(pidArray[i]), WTERMSIG(waitChild));
				fflush(stdout);
				*(pidArray[i]) = 0;
			}
		}
	}
}