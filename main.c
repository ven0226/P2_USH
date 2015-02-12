#include<string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/resource.h>
#include "parse.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <signal.h>

Pipe mainPipe;
char *sysPath;
char *sysHome;
int ushRcFile, oldIn, oldOut, oldErr, oldRcStdin;
char *builtIn[] = { "cd", "echo", "logout", "nice", "pwd", "setenv", "unsetenv","where" };
char *invalidCommand[] = { "", "Command not found", "Permission denied" };
pid_t groupId = 0;
int rcFileProcessing = 0;
extern char **environ;

char cmdPrompt[100];

void processUshCmd(Cmd command);
void signalStpHandler(int signum);

int isBuiltIn(char *command) {
	int i;
	for (i = 0; i < 8; i++) {
		if (!strcmp(command, builtIn[i])) {
			return 1;
		}
	}
	return 0;
}

void saveState() {
	if ((oldIn = dup(fileno(stdin))) == -1) {
		exit(EXIT_FAILURE);
	}
	if ((oldOut = dup(fileno(stdout))) == -1) {
		exit(EXIT_FAILURE);
	}
	if ((oldErr = dup(fileno(stderr))) == -1) {
		exit(EXIT_FAILURE);
	}
}

void resetState() {
	fflush(stderr);
	fflush(stdout);
	if (dup2(oldIn, fileno(stdin)) == -1) {
		exit(EXIT_FAILURE);
	}
	close(oldIn);
	if (dup2(oldOut, fileno(stdout)) == -1) {
		exit(EXIT_FAILURE);
	}
	close(oldOut);
	if (dup2(oldErr, fileno(stderr)) == -1) {
		exit(EXIT_FAILURE);
	}
	close(oldErr);
}

int isDirectory(char *commandPath) {
	int r;
	struct stat *statbuf;
	statbuf = malloc(sizeof(struct stat));
	stat(commandPath, statbuf);
	r = statbuf->st_mode & S_IFDIR;
	free(statbuf);
	return r;
}

void executeCD(Cmd command) {
	char *commandPath;
	if (command->args[1] == NULL) {
		command->args[1] = malloc(strlen(sysHome));
		strcpy(command->args[1], sysHome);
	}
	if (command->args[1][0] == '~') {
		char *remDir;
		int pathLen = 0;
		remDir = strtok(command->args[1], "~");
		if (remDir)
			pathLen = strlen(remDir);
		command->args[1] = malloc(strlen(sysHome) + pathLen);
		strcpy(command->args[1], sysHome);
		strcat(command->args[1], remDir);
	}
	if (isDirectory(command->args[1])) {
		if (command->args[1][0] == '/') {
			chdir("/");
		}
		commandPath = strtok(command->args[1], "/");
		while (commandPath != NULL) {
			if (commandPath[0] != '~') {
				chdir(commandPath);
			}
			commandPath = strtok(NULL, "/");
		}
	} else {
		fprintf(stderr, "invalid directory\n");
	}
}

void executeEcho(Cmd command) {
	int i = 1;
	if (command->nargs > 1) {
		printf("%s", command->args[i]);
		for (i = 2; command->args[i] != NULL; i++)
			printf(" %s", command->args[i]);
	} else
		printf("\n");
	if (i > 1)
		printf("\n");
}

void executeLogout(Cmd command) {
	exit(0);
}

void executeNice(Cmd command) {
	int which, priority;
	id_t pid;
	which = PRIO_PROCESS;
	pid = getpid();
	if (command->args[1] != NULL) {
		priority = atoi(command->args[1]);
		if (priority < -19)
			priority = -19;
		else if (priority > 20)
			priority = 20;
	} else
		priority = 4;
	getpriority(which, 0);
	setpriority(which, 0, priority);
	if (command->args[1] != NULL)
		if (command->args[2] != NULL) {
			Cmd niceCommand;
			niceCommand = malloc(sizeof(struct cmd_t));
			niceCommand->args = malloc(sizeof(char*));
			niceCommand->in = Tnil;
			niceCommand->out = Tnil;
			int j = 0;
			while (j < command->nargs - 2) {
				niceCommand->args[j] = malloc(strlen(command->args[2]));
				strcpy(niceCommand->args[j], command->args[j + 2]);
				j++;
			}
			niceCommand->args[j] = NULL;
			processUshCmd(niceCommand);
			while (j >= 0) {
				free(niceCommand->args[j]);
				j--;
			}
			free(niceCommand->args);
			free(niceCommand);
		}
}

void executepwd(Cmd c) {
	char *directory;
	directory = (char*) get_current_dir_name();
	printf("%s\n", directory);
}

void executeSetenv(Cmd command) {
	int i = 0;
	if (command->args[1] == NULL) {
		while (environ[i]) {
			printf("%s\n", environ[i++]);
		}
		return;
	} else if (command->args[2] == NULL)
		setenv(command->args[1], "", 1);
	else
		setenv(command->args[1], command->args[2], 1);
}

void executeUnSetEnv(Cmd command) {
	if (command->args[1] == NULL) {
		return;
	} else {
		unsetenv(command->args[1]);
	}
}

int isCommand(char *commandPath) {
	if (access(commandPath, F_OK) != 0)
		return 1;
	if (access(commandPath, R_OK | X_OK) != 0 || isDirectory(commandPath))
		return 2;
	return 0;
}

void executeWhere(Cmd command) {
	char *pathPtr;
	char path1[500], commandPath[500] = "";
	if (command->args[1] != NULL) {
		int i = isBuiltIn(command->args[1]);
		if (i != 0)
			printf("%s is a shell built-in\n", command->args[1]);
		strcpy(path1, sysPath);
		pathPtr = strtok(path1, ":");
		while (pathPtr != NULL) {
			strcpy(commandPath, pathPtr);
			strcat(commandPath, "/");
			strcat(commandPath, command->args[1]);
			if (!access(commandPath, X_OK))
				printf("%s\n", commandPath);
			pathPtr = strtok(NULL, ":");
		}
	}
}
void lookUpCommand(Cmd command) {
	if (!strcmp(command->args[0], "cd")) {
		executeCD(command);
	} else if (!strcmp(command->args[0], "echo")) {
		executeEcho(command);
	} else if (!strcmp(command->args[0], "logout")) {
		executeLogout(command);
	} else if (!strcmp(command->args[0], "nice")) {
		executeNice(command);
	} else if (!strcmp(command->args[0], "pwd")) {
		executepwd(command);
	} else if (!strcmp(command->args[0], "setenv")) {
		executeSetenv(command);
	} else if (!strcmp(command->args[0], "unsetenv")) {
		executeUnSetEnv(command);
	} else if (!strcmp(command->args[0], "where")) {
		executeWhere(command);
	} else {
		fprintf(stderr, "Shouldn't get here\n");
	}
}

int isExecutable(Cmd command) {
	fflush(stdout);
	char commandPath[500], tempPath[500];
	strcpy(commandPath, command->args[0]);
	if (strstr(command->args[0], "/") == NULL) {
		char *pathPtr;
		strcpy(tempPath, sysPath);
		pathPtr = strtok(tempPath, ":");
		while (pathPtr != NULL) {
			strcpy(commandPath, pathPtr);
			strcat(commandPath, "/");
			strcat(commandPath, command->args[0]);
			if (!isCommand(commandPath))
				break;
			pathPtr = strtok(NULL, ":");
		}
		strcpy(command->args[0], commandPath);
	}
	int index = isCommand(command->args[0]);
	if (index) {
		fprintf(stderr, "%s\n", invalidCommand[index]);
		return 0;
	}
	return 1;
}

void processUshCmd(Cmd command) {
	if (isBuiltIn(command->args[0])) {
		lookUpCommand(command);
	} else {
		if (isExecutable(command)) {
			int execPid = fork();
			if (execPid == 0) {
				execvp(command->args[0], command->args);
				exit(EXIT_SUCCESS);
			} else {
				int pidStatus;
				if (execPid)
					waitpid(execPid, &pidStatus, 0);
			}
		}
	}
}

void mainShell() {
	char *host = "ush%";
	char ushrcPath[500];
	Pipe localPipe;
	strcpy(ushrcPath, sysHome);
	strcat(ushrcPath, "/.ushrc");
	if ((ushRcFile = open(ushrcPath, O_RDONLY)) != -1) {
		oldRcStdin = dup(fileno(stdin));
		if (oldRcStdin == -1) {
			exit(-1);
		}
		if (dup2(ushRcFile, fileno(stdin)) == -1) {
			exit(-1);
		}
		close(ushRcFile);
		rcFileProcessing = 1;
	} else {
		rcFileProcessing = 0;
	}
	while (1) {
		if (!rcFileProcessing) {
			char hostname[1024];
			hostname[1023] = '\0';
			gethostname(hostname, 1023);
			printf("%s%%", hostname);
			fflush(stdout);
			if (dup2(oldRcStdin, fileno(stdin)) == -1) {
				exit(-1);
			}
		}

		mainPipe = parse();
		if (mainPipe == NULL) {
			rcFileProcessing = 0;
			continue;
		}
		if (!strcmp(mainPipe->head->args[0], "end")) {
			if (rcFileProcessing) {
				rcFileProcessing = 0;
				continue;
			} else
				break;
		}
		localPipe = mainPipe;
		while (localPipe != NULL) {
			int totalPipes = 0;
			Cmd command = localPipe->head;
			Cmd localCmd = command;
			pid_t childList[10];
			int j = 0, i = 0;
			int children = 0;
			int totalCmds = 0;
			int cmdPtr = 0, pipePtr = 0;
			int inFD = 0, outFD = 0, appFD = 0, errFD = 0, appErrFD = 0;
			int inFlag = 0, outFlag = 0, appFlag = 0, errFlag = 0, appErrFlag = 0;
			int lastCmdBuiltinFlag = 0;
			while (localCmd != NULL) {
				if (localCmd->out != Tnil && (localCmd->out == Tpipe || localCmd->out == TpipeErr))
					i++;
				totalCmds++;
				localCmd = localCmd->next;
			}
			totalPipes = i;
			int pipeFd[i * 2];
			while (j < (2 * totalPipes)) {
				pipe(pipeFd + j * 2);
				j++;
			}
			saveState();
			while (command != NULL) {
				if (!strcmp(command->args[0], "end"))
					break;
				if (cmdPtr == totalCmds - 1) {
					if (isBuiltIn(command->args[0])) {
						lastCmdBuiltinFlag = 1;
						if (totalCmds == 1) {
							if (command->in == Tin) {
								inFD = open(command->infile, O_RDONLY);
								dup2(inFD, STDIN_FILENO);
								inFlag = 1;
							}
						}
						if (command->out == Tout) {
							outFD = open(command->outfile, O_CREAT | O_WRONLY | O_TRUNC, 0666);
							dup2(outFD, STDOUT_FILENO);
							outFlag = 1;
						} else if (command->out == ToutErr) {
							errFD = open(command->outfile, O_CREAT | O_WRONLY | O_TRUNC, 0666);
							dup2(errFD, STDERR_FILENO);
							errFlag = 1;
							close(errFD);
							outFD = open(command->outfile, O_CREAT | O_WRONLY | O_TRUNC, 0666);
							dup2(outFD, STDOUT_FILENO);
							close(outFD);
							outFlag = 1;
						} else if (command->out == Tapp) {
							appFD = open(command->outfile, O_APPEND | O_RDWR, S_IREAD | S_IWRITE);
							dup2(appFD, STDOUT_FILENO);
							appFlag = 1;
						} else if (command->out == TappErr) {
							appErrFD = open(command->outfile,O_APPEND | O_WRONLY | S_IWRITE);
							dup2(appErrFD, STDERR_FILENO);
							close(appErrFD);
							appErrFlag = 1;
							appFD = open(command->outfile, O_APPEND | O_RDWR,S_IREAD | S_IWRITE, 0666);
							dup2(appFD, STDOUT_FILENO);
							appFlag = 1;
						}
						if (outFlag || appFlag || errFlag || appErrFlag) {
							if (outFlag)
								close(outFD);
							if (appFlag)
								close(appFD);
							if (errFlag)
								close(errFD);
							if (appErrFlag)
								close(appErrFD);
							outFlag = appFlag = errFlag = appErrFlag = 0;
						}
						if (totalCmds > 1) {
							if (dup2(pipeFd[(pipePtr - 1) * 2], 0) < 0) {
								perror("dup2 read 2");
								exit(EXIT_FAILURE);
							}

							for (i = 0; i < 2 * totalPipes; i++) {
								close(pipeFd[i]);
							}
						}
						processUshCmd(command);
					}
				}
				if (!isBuiltIn(command->args[0])) {
					if (!isExecutable(command)) {
						if (children > 0) {
							killpg(groupId, 9);
						}
						break;
					}
				}
				if (!lastCmdBuiltinFlag) {
					if (0) {

					} else {
						pid_t childPid = fork();
						if (childPid == 0) {
							if (!children) {
								setpgid(0, 0);
							} else {
								setpgid(0, groupId);
							}
							if (totalCmds == 1) {
								if (command->in == Tin) {
									inFD = open(command->infile, O_RDONLY);
									dup2(inFD, STDIN_FILENO);
									inFlag = 1;
								}
								if (command->out == Tout) {
									outFD = open(command->outfile, O_CREAT | O_WRONLY | O_TRUNC, 0666);
									dup2(outFD, STDOUT_FILENO);
									outFlag = 1;
								} else if (command->out == ToutErr) {
									errFD = open(command->outfile, O_CREAT | O_WRONLY | O_TRUNC, 0666);
									dup2(errFD, STDERR_FILENO);
									errFlag = 1;
									close(errFD);
									outFD = open(command->outfile, O_CREAT | O_WRONLY | O_TRUNC, 0666);
									dup2(outFD, STDOUT_FILENO);
									close(outFD);
									outFlag = 1;
								} else if (command->out == Tapp) {
									appFD = open(command->outfile, O_APPEND | O_RDWR, S_IREAD | S_IWRITE);
									dup2(appFD, STDOUT_FILENO);
									appFlag = 1;
								} else if (command->out == TappErr) {
									appErrFD = open(command->outfile,O_APPEND | O_WRONLY | S_IWRITE);
									dup2(appErrFD, STDERR_FILENO);
									close(appErrFD);
									appErrFlag = 1;
									appFD = open(command->outfile, O_APPEND | O_RDWR,S_IREAD | S_IWRITE, 0666);
									dup2(appFD, STDOUT_FILENO);
									appFlag = 1;
								}
								if (inFlag)
									close(inFD);
								if (outFlag)
									close(outFD);
								if (appFlag)
									close(appFD);
								if (errFlag)
									close(errFD);
								if (appErrFlag)
									close(appErrFD);
							}
							if (cmdPtr != 0) {
								if (cmdPtr == totalCmds - 1) {
									if (command->out == Tout) {
										outFD = open(command->outfile, O_CREAT | O_WRONLY | O_TRUNC, 0666);
										dup2(outFD, STDOUT_FILENO);
										outFlag = 1;
									} else if (command->out == ToutErr) {
										errFD = open(command->outfile, O_CREAT | O_WRONLY | O_TRUNC, 0666);
										dup2(errFD, STDERR_FILENO);
										errFlag = 1;
										close(errFD);
										outFD = open(command->outfile, O_CREAT | O_WRONLY | O_TRUNC, 0666);
										dup2(outFD, STDOUT_FILENO);
										close(outFD);
										outFlag = 1;
									} else if (command->out == Tapp) {
										appFD = open(command->outfile, O_APPEND | O_RDWR, S_IREAD | S_IWRITE);
										dup2(appFD, STDOUT_FILENO);
										appFlag = 1;
									} else if (command->out == TappErr) {
										appErrFD = open(command->outfile,O_APPEND | O_WRONLY | S_IWRITE);
										dup2(appErrFD, STDERR_FILENO);
										close(appErrFD);
										appErrFlag = 1;
										appFD = open(command->outfile, O_APPEND | O_RDWR,S_IREAD | S_IWRITE, 0666);
										dup2(appFD, STDOUT_FILENO);
										appFlag = 1;
									}
								}
								if (outFlag || appFlag || errFlag
										|| appErrFlag) {
									if (outFlag)
										close(outFD);
									if (appFlag)
										close(appFD);
									if (errFlag)
										close(errFD);
									if (appErrFlag)
										close(appErrFD);
									outFlag = appFlag = errFlag = appErrFlag =
											0;
								}
								if (dup2(pipeFd[(pipePtr - 1) * 2], 0) < 0) {
									perror("dup2 read");
									exit(EXIT_FAILURE);
								}
							}
							if (command->next != NULL) {
								if (cmdPtr == 0) {
									if (command->in == Tin) {
										inFD = open(command->infile, O_RDONLY);
										dup2(inFD, STDIN_FILENO);
										inFlag = 1;
									}
								}
								if (inFlag) {
									close(inFlag);
									inFlag = 0;
								}
								if(command->out != TpipeErr){
									if (dup2(pipeFd[pipePtr * 2 + 1], 1) < 0) {
										perror("dup2 out");
										exit(EXIT_FAILURE);
									}
								}else{
									if (dup2(pipeFd[pipePtr * 2 + 1], 1) < 0) {
										perror("dup2 output");
										exit(EXIT_FAILURE);
									}
									if (dup2(pipeFd[pipePtr * 2 + 1], 2) < 0) {
										perror("dup2 error");
										exit(EXIT_FAILURE);
									}
								}
							}

							for (i = 0; i < 2 * totalPipes; i++) {
								close(pipeFd[i]);
							}
							processUshCmd(command);
							exit(EXIT_SUCCESS);
						} else {
							if (!children) {
								groupId = childPid;
							}
							childList[children] = childPid;
							children++;
						}
					}
				}
				if (command->out != Tnil && (command->out == Tpipe || command->out == TpipeErr))
					pipePtr++;
				cmdPtr++;
				command = command->next;
			}
			int status = 0;
			for (i = 0; i < 2 * totalPipes; i++) {
				close(pipeFd[i]);
			}
			for (i = 0; i < children; i++) {
				while (-1 == waitpid(childList[i], &status, 0));
			}
			groupId = 0;
			localPipe = localPipe->next;
			resetState();
		}
	}
}

void signalHandler(int signum) {
	if (signum == SIGINT) {
		if (groupId != 0) {
			killpg(groupId, 9);
		}
	}
	signal(SIGINT, signalHandler);
	signal(SIGTSTP, signalStpHandler);
	signal(SIGQUIT, SIG_IGN);
	rcFileProcessing = 0;
}

void signalStpHandler(int signum){
	if (signum == SIGTSTP) {
		if (groupId != 0) {
			killpg(groupId, SIGTSTP);
		}
	}
	signal(SIGTSTP, SIG_IGN);
	signal(SIGINT, signalHandler);
	rcFileProcessing = 0;
	printf("\r");
}

int main(int argc, char *argv[]) {
	sysPath = getenv("PATH");
	sysHome = getenv("HOME");
	signal(SIGTERM, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGINT, signalHandler);
	mainShell();
	return 1;
}
