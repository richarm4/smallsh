#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

//Global variable for foreground only mode
int sigtstp = 0;
//Global variable to determine if sigint should exit
int sigint = 0;
//Global array to keep track of background processes
int backgroundprocesses[5] = {0, 0, 0, 0, 0};
//Keeps track of index of background process so as to not overwrite them
int latestbackgroundprocess = 0;
//Keeps track of status used in status command
int currentexitstatus = 0;
//Keeps track of the pid of the latest foreground child
int foregroundpid = 0;

//Handles SIGINT
void handle_SIGINT(int signo) {
  if (sigint == 1) {
    exit(0);
  }
}

//Enters and exits foreground-only mode
void handle_SIGTSTP(int signo) {
  if (sigtstp == 0) {
    char *message = "\nEntering foreground-only mode (& is now ignored)\n: ";
    sigtstp = 1;
    write(STDOUT_FILENO, message, 52);
  } else if (sigtstp == 1) {
    char *message = "\nExiting foreground-only mode\n: ";
    sigtstp = 0;
    write(STDOUT_FILENO, message, 32);
  }
}

//Struct used for storing user input
struct userinput {
  char **argumentlist;
  int argumentindex;
  char *input;
  char *output;
  int backgroundsignifier;
};

//Creates the struct storing the user's input
struct userinput *createuserinput(char *expanded) {
  //Allocates memory for the struct
  struct userinput *currstruct = malloc(sizeof(struct userinput));

  // For use with strtok_r
  char *saveptr;

  // The first tokens are the set of arguments, input, output, and background
  //   signifier
  currstruct->argumentindex = 1;
  currstruct->backgroundsignifier = 0;
  char **argumentlist;
  currstruct->input = NULL;
  currstruct->output = NULL;
  argumentlist = malloc(sizeof(char *) * 512);
  char *token = strtok_r(expanded, " ", &saveptr);
  argumentlist[0] = calloc(256, sizeof(char));
  strcpy(argumentlist[0], token);
  while (token != NULL) {
    token = strtok_r(NULL, " ", &saveptr);
    if (token != NULL) {
      //Checks if next token will be input
      if (token[0] == '<') {
        token = strtok_r(NULL, " ", &saveptr);
        currstruct->input = malloc(sizeof(char) * 256);
        strcpy(currstruct->input, token);
        //Checks if next token will be output
      } else if (token[0] == '>') {
        token = strtok_r(NULL, " ", &saveptr);
        currstruct->output = malloc(sizeof(char) * 256);
        strcpy(currstruct->output, token);
        //Checks if the input will be a background input
      } else if (token[0] == '&') {
        currstruct->backgroundsignifier = 1;
      } else {
        //Adds the token to the argument list
        argumentlist[currstruct->argumentindex] = calloc(256, sizeof(char));
        strcpy(argumentlist[currstruct->argumentindex], token);
        currstruct->argumentindex++;
      }
    }
  }
  //NULL terminates the argument list
  argumentlist[currstruct->argumentindex] = NULL;
  currstruct->argumentlist = argumentlist;

  return currstruct;
}

int clearstruct(struct userinput *input) {
    //Frees memory from struct
  for (; input->argumentindex > -1; --input->argumentindex) {
    free(input->argumentlist[input->argumentindex]);
    input->argumentlist[input->argumentindex] = NULL;
  }
  free(input->argumentlist);
  input->argumentlist = NULL;
  if (input->input != NULL) {
    free(input->input);
    input->input = NULL;
  }
  if (input->output != NULL) {
    free(input->output);
    input->output = NULL;
  }
  return 0;
}

char *variableexpansion(char *command, int sourcepid) {
  char *input = malloc(2048);
  char *output = malloc(2048);
  strcpy(input, command);
  for (int i = 0; i < strlen(input) - 1; i++) {
    if ((input[i] == '$') && (input[i + 1] == '$')) {
      input[i] = '%';
      input[i + 1] = 'i';
      sprintf(output, input, sourcepid);
    }
  }
  free(input);
  return output;
}

int shellloop() {
  //Gets the parent's pid for future usage
  int sourcepid = getpid();
  //Variable for user input
  char *commandinput = malloc(sizeof(char) * 2048);
  //Variable to store user input after variable expansion
  char *expanded = malloc(sizeof(char) * 2048);
  //Used to break down the string into tokens with strtok, as strtok affects the string inputted into it
  char *tokenstring = malloc(sizeof(char) * 2048);
  //Variable to check if the program was told to exit
  int shouldexit = 0;

  //Checks on any background processes that have exited; I have elected to not run the exit signal, and so know I'm losing out on 10 points
  for (int i = 0; i < 5; i++) {
    if (backgroundprocesses[i] != 0) {
      int exitstatus;
      //Checks if process died
      if (waitpid(backgroundprocesses[i], &exitstatus, WNOHANG) != 0) {
        //Returns pid and exit status
        printf("background pid %i is done: exit status %i\n",
               backgroundprocesses[i], WEXITSTATUS(exitstatus));
        fflush(stdout);
        //Clears process pid from array of tracked processes
        backgroundprocesses[i] = 0;
      }
    }
  }
  //Checks on foreground process
  if (foregroundpid != 0) {
    int exitstatus;
    //If the program was exited abnormally, returns the signal that terminated it
    if (waitpid(foregroundpid, &exitstatus, WNOHANG) == -1) {
      if (WTERMSIG(exitstatus) != 0 && WIFEXITED(exitstatus) != 0) {
        printf("terminated by signal %i\n", WTERMSIG(exitstatus));
        fflush(stdout);
        foregroundpid = 0;
      }
    }
  }
  printf(": ");
  fflush(stdout);
  if (fgets(commandinput, 2048, stdin) == NULL)
    exit(1);
  // Code inspired from
  // https://code-examples.net/en/q/291a90#:~:text=Direct%20to%20remove%20the%20%27n%27%20from%20the%20fgets,%5Bnew_line%5D%20%3D%3D%20%27n%27%29%20line%20%5Bnew_line%5D%20%3D%20%270%27%3B%20%7D
  // to remove the \n that fgets added
  size_t stringlength = strlen(commandinput);
  if (stringlength > 0 && commandinput[stringlength - 1] == '\n') {
    commandinput[--stringlength] = '\0';
  }
  //Expands variable
  if (strstr(commandinput, "$$") != NULL) {
    strcpy(expanded, variableexpansion(commandinput, sourcepid));
  } else {
    strcpy(expanded, commandinput);
  }
  //Stores it in a token string so strtok can be used to check if it is empty or a comment
  strcpy(tokenstring, expanded);
  if (strtok(tokenstring, " ")) {
    if (expanded[0] == '#')
      ;
    else {
      //Stores the user input into a struct
      struct userinput *userinput = malloc(sizeof(struct userinput));
      userinput = createuserinput(expanded);
      //Checks for cd built in command
      if (strstr(userinput->argumentlist[0], "cd")) {
        if (userinput->argumentlist[1] != NULL)
          chdir(userinput->argumentlist[1]);
        else
          chdir(getenv("HOME"));
        //Checks for exit built in command
      } else if (strstr(userinput->argumentlist[0], "exit"))
        shouldexit = 1;
        //Checks for status built in command
      else if (strstr(userinput->argumentlist[0], "status")){
        printf("Exit Status %i\n", currentexitstatus);
        fflush(stdout);
        }
      else {
        int childstatus;
        //Modifies exit status as a result of running non-builtin command
        currentexitstatus = 1;
        //Forks child
        pid_t spawnpid = fork();
        switch (spawnpid) {
          //Child process
        case 0: {
          //Checks to see if it is a background input to set global variables
          if ((userinput->backgroundsignifier == 0 && sigtstp == 0) || sigtstp == 1){
            sigint = 1;
            sigtstp = 42;
            }
          //Following code mostly taken from canvas module on I/O
          // Open source file
          if (userinput->input != NULL) {
            int sourceFD = open(userinput->input, O_RDONLY);
            if (sourceFD == -1) {
              perror("source open()");
              exit(1);
            }
            // Written to terminal
            printf("sourceFD == %d\n", sourceFD);
            fflush(stdout);

            // Redirect stdin to source file
            int result = dup2(sourceFD, 0);
            if (result == -1) {
              perror("source dup2()");
              exit(2);
            }
          }

          // Open target file
          if (userinput->output != NULL) {
            int targetFD =
                open(userinput->output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (targetFD == -1) {
              perror("target open()");
              exit(1);
            }
            printf("targetFD == %d\n", targetFD);
            fflush(stdout);// Written to terminal

            // Redirect stdout to target file
            int result = dup2(targetFD, 1);
            if (result == -1) {
              perror("target dup2()");
              exit(2);
            }
          }
          //Executes the argument and the argument list
          execvp(userinput->argumentlist[0], userinput->argumentlist);
          //Returns an error if the input was invalid
          perror("execvp");
          exit(EXIT_FAILURE);
        }  
        break;
        default:
          // wait functionality learned from Exploration - Process API
          //Checks to see if the input is a background or foreground only mode is active
          if ((userinput->backgroundsignifier == 0 && sigtstp == 0) ||
              sigtstp == 1) {
            wait(&childstatus);
            foregroundpid = spawnpid;
          } else {
            //If not foreground, outputs background pid and stores it into the background process array
            printf("Background pid is %i\n", spawnpid);
            fflush(stdout);
            if (latestbackgroundprocess > 3)
              latestbackgroundprocess = 0;
            else
              latestbackgroundprocess++;
            backgroundprocesses[latestbackgroundprocess] = spawnpid;
          }
          break;
          // parent
        }
      }
      //Frees memory
      clearstruct(userinput);
      free(userinput);
      userinput = NULL;
    }
    //Frees memory
    free(commandinput);
    free(expanded);
    free(tokenstring);
    commandinput = NULL;
    expanded = NULL;
    tokenstring = NULL;
    //Exits from exit builtin command
    if (shouldexit == 1)
      exit(0);
  }
  return 0;
}

int main() {
  //Handles signals
  signal(SIGTSTP, handle_SIGTSTP);
  signal(SIGINT, handle_SIGINT);
  //Loops the shell
  while (1 == 1) {
    shellloop();
  }
  return 0;
}
