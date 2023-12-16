#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/wait.h>
#include "constants.h"
#include "operations.h"
#include "parser.h"

pthread_mutex_t writing_locker;

typedef struct ThreadArgs{
  int thread_id;
  int total_threads;
  char* file_name;
  int fd_out;
  int start_line;
} ThreadArgs;

typedef struct HandlerResult {
    int barrier_state;   // 1 or 0 for boolean signaling
    int curCmd;  // Some integer value
} HandlerResult;

void* handle_commands (void * args){
  ThreadArgs *cmdArgs = (ThreadArgs *)args;
  enum Command cmd;
  int fd_in = open(cmdArgs->file_name, O_RDONLY);
  int thread_id = cmdArgs->thread_id;
  int total_threads = cmdArgs->total_threads;
  int fd_out = cmdArgs->fd_out;
  int start_line = cmdArgs->start_line;
  
  int curCmd = 0; // tem de se mudar para cenários de barrier, em q o 1o comando desta vez é o q vem depois do barrier
  while ((cmd = get_next(fd_in)) != EOC) {
    unsigned int event_id, delay;
    size_t num_rows, num_columns, num_coords;
    size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

    fflush(stdout);

    switch (cmd) {
      case CMD_CREATE:
        
        if (parse_create(fd_in, &event_id, &num_rows, &num_columns) != 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }
        if (curCmd%total_threads==thread_id && curCmd>=start_line){
          if (ems_create(event_id, num_rows, num_columns)) {
            fprintf(stderr, "Failed to create event\n");
          }
        }
        
        break;

      case CMD_RESERVE:
        num_coords = parse_reserve(fd_in, MAX_RESERVATION_SIZE, &event_id, xs, ys);

        if (num_coords == 0) {
          fprintf(stderr, "Failed Reserve. Invalid command. See HELP for usage\n");
          continue;
        }
        if (curCmd%total_threads==thread_id && curCmd>=start_line){
          if (ems_reserve(event_id, num_coords, xs, ys)) {
            fprintf(stderr, "Failed to reserve seats\n");
          }
        }
        
        break;

      case CMD_SHOW:

        
        if (parse_show(fd_in, &event_id) != 0) {
          fprintf(stderr, "Failed Show. Invalid command. See HELP for usage\n");
          continue;
        }
        if (curCmd%total_threads==thread_id && curCmd>=start_line){
          pthread_mutex_lock(&writing_locker);
          if (ems_show(event_id, fd_out)) {
            fprintf(stderr, "Failed to show event\n");
          }
          pthread_mutex_unlock(&writing_locker);
        }
        break;

      case CMD_LIST_EVENTS:
        if (curCmd%total_threads==thread_id && curCmd>=start_line){
          pthread_mutex_lock(&writing_locker);
          if (ems_list_events(fd_out)) {
            fprintf(stderr, "Failed to list events\n");
          }
          pthread_mutex_unlock(&writing_locker);
        }
        break;

      case CMD_WAIT:
          unsigned int target_thread_id;
          int has_thread_id = parse_wait(fd_in, &delay, &target_thread_id);
          if (curCmd>=start_line && parse_wait(fd_in, &delay, &target_thread_id)>0){
              if (has_thread_id == -1) {  
              fprintf(stderr, "Invalid command. See HELP for usage\n");
            } else if (has_thread_id && (int)target_thread_id == thread_id+1 && delay > 0){
              printf("Waiting...\n");
              ems_wait(delay);
            } else if (!has_thread_id && delay > 0){
              printf("Waiting...\n");
              ems_wait(delay);  
            }
          }
          
          break;

      case CMD_INVALID:
        if (curCmd>=start_line){
          fprintf(stderr, "Invalido. Invalid command. See HELP for usage\n");
        }
        break;

      case CMD_HELP:
        if (curCmd>=start_line){
          printf(
            "Available commands:\n"
            "  CREATE <event_id> <num_rows> <num_columns>\n"
            "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
            "  SHOW <event_id>\n"
            "  LIST\n"
            "  WAIT <delay_ms> [thread_id]\n"  // thread_id is not implemented
            "  BARRIER\n"                      // Not implemented
            "  HELP\n");
        }
        break;

      case CMD_BARRIER:
        if (curCmd>=start_line){
          struct HandlerResult *state_curCmd = malloc(sizeof(struct HandlerResult));
          state_curCmd->barrier_state = BARRIER_ON;
          state_curCmd->curCmd = curCmd+1;
          pthread_exit(state_curCmd);
          free(state_curCmd);
        }
        break;
      case CMD_EMPTY:
        break;

      case EOC:
        break;
    }
    curCmd++;
  }
  return NULL;
}

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;
  char *dir_str;
  int max_proc, max_threads;
  
  DIR * dirp;
  struct dirent * file_searcher;
  //int fd;

  //Get all arguments (directory, max_proc, max_thread, delay)
  if (argc > 4) {
    char *endptr;
    unsigned long int delay = strtoul(argv[4], &endptr, 10);
    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }
    state_access_delay_ms = (unsigned int)delay;
  }
  if (ems_init(state_access_delay_ms)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }
  dir_str = argv[1];
  dirp = opendir(dir_str);
  if (dirp == NULL) {
    fprintf(stderr, "Error opening directory");
    closedir(dirp);
    return 1;
  }
  max_proc = atoi(argv[2]);
  if (max_proc <= 0) {
    fprintf(stderr, "Invalid value for maximum number of processes\n");
    return 1;
  }
  max_threads = atoi(argv[3]);
  if (max_threads <= 0) {
    fprintf(stderr, "Invalid value for maximum number of threads\n");
    return 1;
  }

  int proc_count = 0;
  while ((file_searcher = readdir(dirp))!= NULL){
    size_t file_name_length = strlen(file_searcher->d_name);

    if (strcmp(file_searcher->d_name + strlen(file_searcher->d_name) - 5, ".jobs") == 0){
      if (proc_count >= max_proc){
        int status;
        pid_t terminated_pid = wait(&status);
        if (terminated_pid > 0){
          printf("Child process %d terminated\n", terminated_pid);
          proc_count--;
        }
      }

      // fork, create a new process for the current file being pointed by file_searcher
      pid_t pid = fork();

      if (pid == -1){
        fprintf(stderr, "Failed to fork\n");
        return 1;
      } 

      else if (pid == 0){
        pthread_t tids [max_threads];

        proc_count++;
        
        char* file_path = malloc((strlen(dir_str)+ strlen(file_searcher->d_name)+2)*sizeof(char));
        strcpy(file_path, dir_str);
        strcat(file_path, file_searcher->d_name);

        
        int fd_in = open(file_path, O_RDONLY);
        if (fd_in < 0){
          fprintf(stderr, "open error: %s\n", strerror(errno));
          return -1;
        }

        char *file_name = strndup(file_searcher->d_name, file_name_length - 5);
        char *extension = ".out";
        file_name = (char *)realloc(file_name, (strlen(file_name) + strlen(extension) + 1) * sizeof(char));
        strcat(file_name, extension);
        if (file_name == NULL) {
          fprintf(stderr, "Memory allocation error\n");
          return 1;
        }
        char* file_out_path = (char*) malloc((strlen(dir_str)+ strlen(file_name) + 1) * sizeof(char));
        if (file_out_path == NULL) {
          fprintf(stderr, "Memory allocation error\n");
          return 1;
        }
        strcpy(file_out_path, dir_str);
        strcat(file_out_path, file_name);

        int fd_out = open(file_out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644); //trocar var p fd_out pq é um file descriptor
        if (fd_out < 0){
          fprintf(stderr, "Failed to open file .out. Error: %s", strerror(errno));
          return 1;
        }

        int barrier = BARRIER_ON;
        int curCmd = 0;
        while (barrier == BARRIER_ON){
          barrier = BARRIER_OFF;
          HandlerResult *thread_result;
          ThreadArgs *args = (ThreadArgs*) malloc(sizeof(ThreadArgs) * (size_t)max_threads); 

          if (args == NULL) {
              fprintf(stderr, "Memory allocation error\n");
              break;
          }

          // create all threads
          for (int num_threads = 0; num_threads < max_threads; num_threads++){
            args[num_threads].thread_id = num_threads;
            args[num_threads].file_name = file_path;
            args[num_threads].total_threads = max_threads;
            args[num_threads].fd_out = fd_out;
            args[num_threads].start_line = curCmd;
            if (pthread_create(&tids[num_threads],NULL, handle_commands, (void *)&args[num_threads]) != 0){
              fprintf(stderr, "error creating thread.\n");
              continue;
            }
          }
          //wait for all threads
          for (int i = 0; i < max_threads; ++i) {
            pthread_join(tids[i], (void **)&thread_result);
            if (thread_result != NULL) {
              int barrier_state = thread_result->barrier_state;
              if (barrier_state == BARRIER_ON) {
                barrier = thread_result->barrier_state;
                curCmd = thread_result->curCmd;
              }
            }
            free(thread_result);
          }
          if (barrier ==1)
          free(args);
        }
        
        free(file_out_path);
        free(file_name);
        free(file_path);

        close(fd_in);
        close(fd_out);
        exit(0);
      } else {
        // Parent process
        proc_count++;
      }
    }
  }

  // Wait for all remaining child processes to finish
  while (proc_count > 0) {
    int status;
    pid_t terminated_pid = wait(&status);
    if (terminated_pid > 0) {
      printf("Child process %d terminated\n", terminated_pid);
      proc_count--;
    }
  }
  ems_terminate();
  closedir(dirp);
  return 0;
}