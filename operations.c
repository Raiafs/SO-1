#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include "eventlist.h"
#include "constants.h"

typedef struct {
    size_t x;
    size_t y;
} Coordinate;

static struct EventList* event_list = NULL;
static unsigned int state_access_delay_ms = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int target_thread_id;

/// Calculates a timespec from a delay in milliseconds.
/// @param delay_ms Delay in milliseconds.
/// @return Timespec with the given delay.
static struct timespec delay_to_timespec(unsigned int delay_ms) {
  return (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
}

/// Gets the event with the given ID from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event_id The ID of the event to get.
/// @return Pointer to the event if found, NULL otherwise.
static struct Event* get_event_with_delay(unsigned int event_id) {
  struct timespec delay = delay_to_timespec(state_access_delay_ms);
  nanosleep(&delay, NULL);  // Should not be removed

  return get_event(event_list, event_id);
}

/// Gets the seat with the given index from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event Event to get the seat from.
/// @param index Index of the seat to get.
/// @return Pointer to the seat.
static unsigned int* get_seat_with_delay(struct Event* event, size_t index) {
  struct timespec delay = delay_to_timespec(state_access_delay_ms);
  nanosleep(&delay, NULL);  // Should not be removed

  return &event->data[index];
}

static pthread_mutex_t* get_lock_with_delay(struct Event* event, size_t index) {
  struct timespec delay = delay_to_timespec(state_access_delay_ms);
  nanosleep(&delay, NULL);  // Should not be removed

  return &event->mutex[index];
}

/// Gets the index of a seat.
/// @note This function assumes that the seat exists.
/// @param event Event to get the seat index from.
/// @param row Row of the seat.
/// @param col Column of the seat.
/// @return Index of the seat.
static size_t seat_index(struct Event* event, size_t row, size_t col) { return (row - 1) * event->cols + col - 1; }

int compare_coordinates(const void *a, const void *b) {
    const Coordinate *coord1 = (const Coordinate *)a;
    const Coordinate *coord2 = (const Coordinate *)b;

    if (coord1->x < coord2->x) {
        return -1;
    } else if (coord1->x > coord2->x) {
        return 1;
    } else {
        // If x-values are equal, compare using y-values
        if (coord1->y < coord2->y) {
            return -1;
        } else if (coord1->y > coord2->y) {
            return 1;
        } else {
            return 0;
        }
    }
}

int ems_init(unsigned int delay_ms) {
  if (event_list != NULL) {
    fprintf(stderr, "EMS state has already been initialized\n");
    return 1;
  }

  event_list = create_list();
  state_access_delay_ms = delay_ms;

  return event_list == NULL;
}

int ems_terminate() {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  free_list(event_list);
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (get_event_with_delay(event_id) != NULL) {
    fprintf(stderr, "Event already exists\n");
    return 1;
  }

  struct Event* event = malloc(sizeof(struct Event));

  if (event == NULL) {
    fprintf(stderr, "Error allocating memory for event\n");
    return 1;
  }

  event->id = event_id;
  event->rows = num_rows;
  event->cols = num_cols;
  event->reservations = 0;
  event->data = malloc(num_rows * num_cols * sizeof(unsigned int));
  event->mutex = malloc(num_rows * num_cols * sizeof(pthread_mutex_t));

  if (event->data == NULL) {
    fprintf(stderr, "Error allocating memory for event data\n");
    free(event);
    return 1;
  }

  for (size_t i = 0; i < num_rows * num_cols; i++) {
    event->data[i] = 0;
  }

  for (size_t i = 0; i < num_rows * num_cols; i++) {
    event->mutex[i] = PTHREAD_MUTEX_INITIALIZER;
  }

  if (append_to_list(event_list, event) != 0) {
    fprintf(stderr, "Error appending event to list\n");
    free(event->data);
    free(event->mutex);
    free(event);
    return 1;
  }

  return 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id);

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }
  Coordinate coordinates[num_seats];
  for (int i=0; i<(int)num_seats;i++){
    coordinates[i].x= xs[i];
    coordinates[i].y= ys[i];
  }

  qsort(coordinates, num_seats, sizeof(Coordinate), compare_coordinates);

  for (int i=0; i< (int)num_seats;i++){
    xs[i]=coordinates[i].x;
    ys[i]=coordinates[i].y;
  }

  unsigned int reservation_id = ++event->reservations;

  size_t e =0;
  for(; e<num_seats; e++){
    size_t row = xs[e];
    size_t col = ys[e];
    printf("a");
    pthread_mutex_lock(get_lock_with_delay(event, seat_index(event, row, col)));
    if (row <= 0 || row > event->rows || col <= 0 || col > event->cols) {
      fprintf(stderr, "Invalid seat\n");
      break;
    }
    if (*get_seat_with_delay(event, seat_index(event, row, col)) != 0) {
      fprintf(stderr, "Seat already reserved\n");
      //add remover todos os locks
      break;
    }
  }
  size_t j=0;
  if (e < num_seats) {
    event->reservations--;
    for (j = 0; j < e; j++) {
      pthread_mutex_unlock(get_lock_with_delay(event, seat_index(event, xs[j], ys[j])));
    }
    return 1;
  }

  size_t i = 0;
  for (; i < num_seats; i++) {
    size_t row = xs[i];
    size_t col = ys[i];
    *get_seat_with_delay(event, seat_index(event, row, col)) = reservation_id;
  }

  // If the reservation was not successful, free the seats that were reserved.
  if (i < num_seats) {
    event->reservations--;
    for (j = 0; j < i; j++) {
      *get_seat_with_delay(event, seat_index(event, xs[j], ys[j])) = 0;
    }
    for (j = 0; j < e; j++) {
      pthread_mutex_unlock(get_lock_with_delay(event, seat_index(event, xs[j], ys[j])));
    }
    return 1;
  }

  for (j = 0; j < e; j++) {
    pthread_mutex_unlock(get_lock_with_delay(event, seat_index(event, xs[j], ys[j])));
  }
  return 0;
}

int ems_show(unsigned int event_id, int fd_out) {
  size_t len;
  int done;

  struct Event* event = get_event_with_delay(event_id);

  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }


  size_t x =0;
  for(; x<event->cols; x++){
    size_t y =0;
    for(; y<event->rows; y++)
    pthread_mutex_lock(get_lock_with_delay(event, seat_index(event, x, y)));
  }
  printf("entered show.\n");

  for (size_t i = 1; i <= event->rows; i++) {
    for (size_t j = 1; j <= event->cols; j++) {
      unsigned int* seat = get_seat_with_delay(event, seat_index(event, i, j));

      char *str = (char*) malloc(sizeof(*seat));

      if (str == NULL) {
        fprintf(stderr, "Memory allocation error\n");
        for(x=0; x<event->cols; x++){
          size_t y =0;
          for(; y<event->rows; y++)
            pthread_mutex_lock(get_lock_with_delay(event, seat_index(event, x, y)));
        }
        return 1;
      }

      sprintf(str, "%u", *seat);

      len = strlen(str);
      done = 0;
      while (len > 0) {
        ssize_t bytes_written = write(fd_out, str + done, len);

        if (bytes_written < 0){
          fprintf(stderr, "write error: %s\n", strerror(errno));
          for(x=0; x<event->cols; x++){
           size_t y =0;
            for(; y<event->rows; y++)
              pthread_mutex_lock(get_lock_with_delay(event, seat_index(event, x, y)));
          }
          return -1;
        }

        /* might not have managed to write all, len becomes what remains */
        len -= (size_t) bytes_written;
        done += (int) bytes_written;
      }

      if (j < event->cols) {

        len = strlen(" ");
        done = 0;
        while (len > 0) {
          ssize_t bytes_written = write(fd_out, " " + done, len);

          if (bytes_written < 0){
            fprintf(stderr, "write error: %s\n", strerror(errno));
            for(x=0; x<event->cols; x++){
              size_t y =0;
              for(; y<event->rows; y++)
                pthread_mutex_lock(get_lock_with_delay(event, seat_index(event, x, y)));
            }
            return -1;
          }

          /* might not have managed to write all, len becomes what remains */
          len -= (size_t) bytes_written;
          done += (int) bytes_written;
        }
      }

      free(str);
    }

    len = strlen("\n");
    done = 0;
    while (len > 0) {
      ssize_t bytes_written = write(fd_out, "\n" + done, len);

      if (bytes_written < 0){
        fprintf(stderr, "write error: %s\n", strerror(errno));
        for(x=0; x<event->cols; x++){
          size_t y =0;
          for(; y<event->rows; y++)
            pthread_mutex_lock(get_lock_with_delay(event, seat_index(event, x, y)));
        }
        return -1;
      }

      /* might not have managed to write all, len becomes what remains */
      len -= (size_t) bytes_written;
      done += (int) bytes_written;
    }
  }
  for(x=0; x<event->cols; x++){
    size_t y =0;
      for(; y<event->rows; y++)
        pthread_mutex_lock(get_lock_with_delay(event, seat_index(event, x, y)));
  }
  return 0;
}

int ems_list_events(int fd_out) {
  size_t len;
  int done;

  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (event_list->head == NULL) {

      len = strlen(MSG_NO_EVENTS);
      done = 0;
      while (len > 0) {
        ssize_t bytes_written = write(fd_out, MSG_NO_EVENTS + done, len);

        if (bytes_written < 0){
          fprintf(stderr, "write error: %s\n", strerror(errno));
          return -1;
        }

        /* might not have managed to write all, len becomes what remains */
        len -= (size_t) bytes_written;
        done += (int) bytes_written;
      }

    return 0;
  }

  struct ListNode* current = event_list->head;
  while (current != NULL) {
    size_t str_length = (size_t) snprintf(NULL, 0, "Event: %u\n", (current->event)->id) + 1;  
    char* str = (char*) malloc(str_length);

    if (str == NULL) {
      fprintf(stderr, "Memory allocation error\n");
      return 1;
    }

    sprintf(str, "Event: %u\n", (current->event)->id);

    len = strlen(str);
    done = 0;
    while (len > 0) {
        ssize_t bytes_written = write(fd_out, str + done, len);

        if (bytes_written < 0){
          fprintf(stderr, "write error: %s\n", strerror(errno));
          return -1;
        }

        /* might not have managed to write all, len becomes what remains */
        len -= (size_t) bytes_written;
        done += (int) bytes_written;
    }

    current = current->next;
    
    free(str);
  }

  return 0;
}


void ems_wait(unsigned int delay_ms) {
    struct timespec delay = {delay_ms / 1000, \
                    (delay_ms % 1000) * 1000000}; //{Seconds, Nanoseconds} Converted from miliseconds
    nanosleep(&delay, NULL);  // Sleep for the specified delay
}