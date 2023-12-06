#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
#include "constants.h"
#include "operations.h"
#include "parser.h"

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;
  char *dir_str;
  
  DIR * dirp;
  struct dirent * file_searcher;
  //int fd;

  
  if (argc > 1) {
    char *endptr;
    unsigned long int delay = strtoul(argv[1], &endptr, 10);

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

  dir_str = argv[2];
  dirp = opendir(dir_str);
  if (dirp == NULL) {
        fprintf(stderr, "Error opening directory");
        closedir(dirp);
        return 1;
  }

  while ((file_searcher = readdir(dirp))!= NULL){
    if (strcmp(file_searcher->d_name + strlen(file_searcher->d_name) - 5, ".jobs") == 0){
      char* file_path = malloc((strlen(dir_str)+ strlen(file_searcher->d_name)+2)*sizeof(char));
      strcpy(file_path, dir_str);
      //printf("%s\n", file_searcher->d_name);
      strcat(file_path, file_searcher->d_name);
      printf("%s\n", file_path);
      

      int fd = open(file_path, O_RDONLY);
      if (fd < 0){
        fprintf(stderr, "open error: %s\n", strerror(errno));
        return -1;
      }
      
      enum Command cmd;
      while ((cmd = get_next(fd)) != EOC) {
        unsigned int event_id, delay;
        size_t num_rows, num_columns, num_coords;
        size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

        printf(">");
        fflush(stdout);
       

        switch (cmd) {
          case CMD_CREATE:
            printf("create entered\n");
            if (parse_create(fd, &event_id, &num_rows, &num_columns) != 0) {
              fprintf(stderr, "Invalid command. See HELP for usage\n");
              continue;
            }

            if (ems_create(event_id, num_rows, num_columns)) {
              fprintf(stderr, "Failed to create event\n");
            }

          break;

          case CMD_RESERVE:
            printf("reserve entered\n");
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
            printf("show entered\n");
            if (parse_show(fd, &event_id) != 0) {
              fprintf(stderr, "Invalid command. See HELP for usage\n");
              continue;
            }

            if (ems_show(event_id)) {
              fprintf(stderr, "Failed to show event\n");
            }

          break;

          case CMD_LIST_EVENTS:
            if (ems_list_events()) {
              fprintf(stderr, "Failed to list events\n");
            }

          break;

          case CMD_WAIT:
            if (parse_wait(fd, &delay, NULL) == -1) {  // thread_id is not implemented
              fprintf(stderr, "Invalid command. See HELP for usage\n");
              continue;
            }

            if (delay > 0) {
              printf("Waiting...\n");
              ems_wait(delay);
            }

          break;

          case CMD_INVALID:
            fprintf(stderr, "Invalid command. See HELP for usage\n");
          break;

          case CMD_HELP:
            printf(
                "Available commands:\n"
                "  CREATE <event_id> <num_rows> <num_columns>\n"
                "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
                "  SHOW <event_id>\n"
                "  LIST\n"
                "  WAIT <delay_ms> [thread_id]\n"  // thread_id is not implemented
                "  BARRIER\n"                      // Not implemented
                "  HELP\n");

          break;

          case CMD_BARRIER:  // Not implemented
          case CMD_EMPTY:
          break;

          case EOC:
            
          break;
        }
      }
      close(fd);
      free(file_path);
    }
  }
  ems_terminate();
  closedir(dirp);
  return 0;
}