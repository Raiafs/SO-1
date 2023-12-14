#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/wait.h>
#include "constants.h"
#include "operations.h"
#include "parser.h"

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;
  char *dir_str;
  int proc_count = 0;
  
  DIR * dirp;
  struct dirent * file_searcher;
  //int fd;

  if (argc > 3) {
    char *endptr;
    unsigned long int delay = strtoul(argv[3], &endptr, 10);

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

  int max_proc = atoi(argv[2]);
  if (max_proc <= 0) {
    fprintf(stderr, "Invalid value for maximum number of processes\n");
    return 1;
  }

  dir_str = argv[1];
  dirp = opendir(dir_str);
  if (dirp == NULL) {
        fprintf(stderr, "Error opening directory");
        closedir(dirp);
        return 1;
  }

  while ((file_searcher = readdir(dirp))!= NULL){
    size_t file_name_length = strlen(file_searcher->d_name);
    
    if (strcmp(file_searcher->d_name + strlen(file_searcher->d_name) - 5, ".jobs") == 0){

        if (proc_count >= max_proc){
            int status;
            pid_t terminated_pid = wait(&status);
            if (terminated_pid > 0){
                //printf("Child process %d terminated\n", terminated_pid);
                proc_count--;
            }
        }

        pid_t pid = fork();

        if (pid == -1){

            fprintf(stderr, "Failed to fork\n");
            return 1;

        } else if (pid == 0){

            proc_count++;
            
            char* file_path = (char*) malloc((strlen(dir_str) + strlen(file_searcher->d_name) + 2 ) * sizeof(char));

            if (file_path == NULL) {
                fprintf(stderr, "Memory allocation error\n");
                return 1;
            }


            strcpy(file_path, dir_str);
            strcat(file_path, file_searcher->d_name);
                 
            int fd = open(file_path, O_RDONLY);

            if (fd < 0){
                fprintf(stderr, "open error: %s\n", strerror(errno));
                return 1;
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

            int file_out = open(file_out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

            if (file_out < 0){
                fprintf(stderr, "Failed to open file .out. Error: %s", strerror(errno));
                return 1;
            }

            write(file_out, file_path, strlen(file_path));
            write(file_out, "\n", 1);

            enum Command cmd;
            while ((cmd = get_next(fd)) != EOC) {
                unsigned int event_id, delay;
                size_t num_rows, num_columns, num_coords;
                size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

                switch (cmd) {
                    case CMD_CREATE:
                        write(file_out, MSG_CREATE, strlen(MSG_CREATE));

                        if (parse_create(fd, &event_id, &num_rows, &num_columns) != 0) {
                            fprintf(stderr, "Invalid command. See HELP for usage\n");
                            continue;
                        }

                        if (ems_create(event_id, num_rows, num_columns)) {
                            fprintf(stderr, "Failed to create event\n");
                        }

                        break;

                    case CMD_RESERVE:
                        write(file_out, MSG_RESERVE, strlen(MSG_RESERVE));

                        num_coords = parse_reserve(fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);

                        if (num_coords == 0) {
                            fprintf(stderr, "Invalid command. See HELP for usage\n");
                            continue;
                        }

                        if (ems_reserve(event_id, num_coords, xs, ys)) {
                            fprintf(stderr, "Failed to reserve seats\n");
                        }

                        break;

                    case CMD_SHOW:
                        write(file_out, MSG_SHOW, strlen(MSG_SHOW));

                        if (parse_show(fd, &event_id) != 0) {
                            fprintf(stderr, "Invalid command. See HELP for usage\n");
                            continue;
                        }

                        if (ems_show(event_id, file_out)) {
                            fprintf(stderr, "Failed to show event\n");
                        }

                        break;

                    case CMD_LIST_EVENTS:
                        if (ems_list_events(file_out)) {
                            fprintf(stderr, "Failed to list events\n");
                        }

                        break;

                    case CMD_WAIT:
                        if (parse_wait(fd, &delay, NULL) == -1) {  // thread_id is not implemented
                            fprintf(stderr, "Invalid command. See HELP for usage\n");
                            continue;
                        }

                        if (delay > 0) {
                            write(file_out, MSG_WAITING, strlen(MSG_WAITING));
                            
                            ems_wait(delay);
                        }

                        break;

                    case CMD_INVALID:
                        fprintf(stderr, "Invalid command. See HELP for usage\n");

                        break;

                    case CMD_HELP:
                        write(file_out, MSG_HELPER, strlen(MSG_HELPER));

                        break;

                    case CMD_BARRIER:  // Not implemented

                    case CMD_EMPTY:
                        break;

                    case EOC:
                    
                        break;
                }
            }

            pid_t child_pid = getpid();
            size_t str_length = (size_t) snprintf(NULL, 0, "Child process %d terminated\n", child_pid) + 1;

            char *msg_process_terminated = (char*) malloc(str_length);

            if (msg_process_terminated == NULL) {
                fprintf(stderr, "Memory allocation error\n");
                return 1;
            }

            sprintf(msg_process_terminated, "Child process %d terminated\n", child_pid);
            write(file_out, msg_process_terminated, strlen(msg_process_terminated));

            free(msg_process_terminated);
            close(file_out);
            free(file_out_path);
            free(file_name);
            close(fd);
            free(file_path);
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
          //printf("Child process %d terminated\n", terminated_pid);
          proc_count--;
        }
      }

  ems_terminate();
  closedir(dirp);
  return 0;
}