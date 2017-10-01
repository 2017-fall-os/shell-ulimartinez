//
// Created by ulimartinez on 9/12/17.
//
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <wait.h>
#include <stdlib.h>
#include "mytoc.h"
#include "util.h"

//function prototypes

char* getEnvVar(char **envp, char *var);

short checKDir(char *path);

short checkComm(char *path);

char* getPrompt();

void freeVec(char **);

char * getAbsComm(char *com, char **paths, char **envp);

int main(int argc, char **argv, char **envp){
    int pid;
    int waitVal, waitStatus;
    char **paths = NULL;
    int *pipedes = calloc(2, sizeof(int));
    chdir(getEnvVar(envp, "HOME"));//set the initial directory of the shell to the $HOME environment variable
    int cstdin = dup(STDIN_FILENO);//save a copy of the stdin file descriptor
    int cstdout = dup(STDOUT_FILENO);//and the stdout as well
    while(1){//inf loop to keep on prompting
        char *prompt = getPrompt();//the prompt has the cwd, so reset it in case cwd changed
        char *command = NULL;
        write(STDOUT_FILENO, prompt, strlen(prompt));//print the prompt to standard out
        char *str = getStdIn();//get input from user
        //fflush(stdin);
        if(streq(str, "exit"))//check for exit string
            return 0;
        char **pipes = mytoc(str, PIPE_DELIM);//separate the input by commands that use pipes
        if(veclen(pipes) > 0){
            char **tokens = mytoc(pipes[0], SPACE_DELIM);//tokenize the input from the prompt
            if(tokens){
                if(streq(tokens[0], "cd")){//if the first token is cd then
                    if(checKDir(tokens[1])){//check if the given path is a directory
                        if(!chdir(tokens[1]))//change to that directory
                            continue;//skip to next iteration of loop i.e. ask for next command
                    }
                }
                command = getAbsComm(tokens[0], paths, envp);//get the absolute path of the command typed
                if(!command)
                    printf("%s: command not found\n", tokens[0]);
                else{//if the command was found create a child process to run the command
                    pid = fork();
                    if(pid == 0){//if child
                        if(veclen(pipes) > 1){//case where we will need to pipe the output of the child
                            pipe(pipedes);//setup our pipe
                            close(pipedes[0]);//close the read end of the pipe (wont read anything from it)
                            dup2(pipedes[1], STDOUT_FILENO);//dup the write end of the pipe to stdout, dup2 closes the fd if it is open
                            close(pipedes[1]);//close the write end of the pipe TODO: don't close it if you will write to it again
                        }
                        execve(command, tokens, envp);
                    }
                    else{//if parent
                        if(!streq(tokens[veclen(tokens)-1], "&")){//if the last token is a '&', run in background by not waiting for child
                            waitVal = waitpid(pid, &waitStatus, 0);//wait until child terminates
                            if(waitVal == pid && waitStatus != 0){//if the terminated child notify the exit code
                                printf("Program terminated with exit code %d\n", waitStatus);
                            }
                        }
                        if(veclen(pipes) > 1){//case where we are using a pipe
                            close(pipedes[1]);//close the write end of the pipe
                            dup2(pipedes[0], STDIN_FILENO);//dup the read end of the pipe to stdin
                            pid = fork();//create a new child process that will read input from the pipe
                            if(pid == 0){//second child
                                char **tokens2 = mytoc(pipes[1], SPACE_DELIM);//tokenize what's after the pipe
                                char *command2 = getAbsComm(tokens2[0], paths, envp);
                                if(command2)
                                    execve(command2, tokens2, envp);
                                else{
                                    printf("%s: command not found\n", tokens2[0]);
                                }
                                //cleanup
                                free(command2);
                                freeVec(tokens2);
                            }
                            else{
                                waitVal = (pid, &waitStatus, 0);//wait until child terminates
                                if(waitVal == pid){//if the terminated child notify the exit code
                                    printf("Program terminated with exit code %d\n", waitStatus);
                                }
                                close(pipedes[0]);//close the read end of pipe
                                dup2(cstdin, STDIN_FILENO);//reset the fd 0
                                dup2(cstdout, STDOUT_FILENO);//reset fd 1
                            }
                        }
                    }
                }
                //cleanup of memory
                free(command);
                free(str);
                freeVec(tokens);
                free(prompt);
            }
        }
    }
}
/*
 * free a vector of char pointers
 */
void freeVec(char **vec){
    for(char **pVec = vec; *pVec; pVec++){
        if(*pVec)
            free(*pVec);
    }
    free(vec);
}
/*
 * uses stat to check that a file exists and also if the file is an executable file
 */
short checkComm(char *path){
    struct stat sb;
    return(((stat(path, &sb) == 0) && (sb.st_mode & S_IXOTH))?(short)1:(short)0);
}
/*
 * uses stat to check that a file directory exists
 */
short checKDir(char *path){
    struct stat sb;
    return(((stat(path, &sb) == 0) && (sb.st_mode & S_IFDIR))?(short)1:(short)0);
}
/*
 * Gets the path variable from the environment variables
 */
char* getEnvVar(char **envp, char *var){
    for(int i = 0; envp[i] != (char *)0; i++){
        char **toks = mytoc(envp[i], EQ_DELIM);
        if(streq(toks[0], var))
            return toks[1];
    }
    return NULL;
}
/*
 * gets a prompt that includes cwd
 */
char* getPrompt(){
    char *buf = malloc(MAX_CHARS);
    char *ptr = getcwd(buf, MAX_CHARS);
    char *prompt = mystrcat(ptr, "$");
    free(buf);
    return prompt;
}
/*
 * returns the absolute path of a command or NULL if not found
 */
char * getAbsComm(char *com, char **paths, char **envp){
    if(checkComm(com)) {
        return substrCopy(com, strlen(com));
    }
    else{
        if(!paths){//keep a copy of the path to not tokenize it every time
            char *path = getEnvVar(envp, "PATH");
            paths = mytoc(path, COLON_DELIM);//tokenize the $PATH env variable, each token is a directory with binary files
        }
        for(int i = 0; paths[i] != (char *)0; i++){//look for the command in all the path directories
            char *str = mystrcat(paths[i], "/");
            char *comm = mystrcat(str, com);
            free(str);
            if(checkComm(comm)) {//once found store the full path
                return comm;
            }
        }
        return NULL;
    }
}