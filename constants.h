#define MAX_RESERVATION_SIZE 256
#define STATE_ACCESS_DELAY_MS 10

#define MSG_CREATE "create entered\n"
#define MSG_RESERVE "reserve entered\n"
#define MSG_SHOW "show entered\n"
#define MSG_NO_EVENTS "No events\n"
#define MSG_WAITING "Waiting...\n"
#define MSG_HELPER "Available commands:\n\
                              CREATE <event_id> <num_rows> <num_columns>\n\
                              RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n\
                              SHOW <event_id>\n\
                              LIST\n\
                              WAIT <delay_ms> [thread_id]\n\
                              BARRIER\n\
                              HELP\n;"
