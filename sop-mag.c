#include "l8_common.h"
#include <unistd.h> // dla usleep

#define SPELL_TYPES 3
const char* spell_names[SPELL_TYPES] = {"Divination", "Summon Elemental", "Fireball"};
const int spell_costs[SPELL_TYPES] = {1, 3, 4}; // KOSZTY ZAKLĘĆ z zadania

#define BOARD_SIZE 8
#define BACKLOG 16

#define MAX_QUEUE 10
#define THREAD_COUNT 3
#define FAMILIAR_DELAY 100

#define MAX_CLIENTS 2
#define MAX_NAME_LENGTH 14
#define STARTING_PEBBLES 10 // Początkowa liczba kamyków

volatile sig_atomic_t isRunning = 1;

void sigint_handler() {
    isRunning = 0;
}

typedef struct __attribute__((__packed__)) packed {
    char c1;
    int i1;
    char c2;
    int i2;
} packed_t;

typedef struct not_packed {
    char c1;
    int i1;
    char c2;
    int i2;
} not_packed_t;

typedef struct __attribute__((__packed__)) message {
    char type;
    char padding;
    char message[MAX_NAME_LENGTH];
} message_t;


typedef struct player {
    struct sockaddr_in addr;
    char name[MAX_NAME_LENGTH + 1];
    int pebbles;
} player_t;


typedef struct task {
    message_t message;
    int player_id;
} task_t;

typedef struct fifo {
    task_t elements[MAX_QUEUE];
    int front;
    int rear;
    int count;
} fifo_t;

typedef struct server_data {
    fifo_t fifo;
    pthread_mutex_t* fifo_mutex;
    pthread_cond_t* fifo_cond;

    player_t players[MAX_CLIENTS];
    pthread_mutex_t* players_mutex;
    int* players_count;
} server_data_t;

void usage(char* name) {
    printf("%s <in_port>\n", name);
    printf("  in_port - port that accepts messages\n");
    exit(EXIT_FAILURE);
}

void doServer(int fd, server_data_t* data) {
    struct sockaddr_in addr;
    message_t* message;
    char buf[MAX_NAME_LENGTH + 2];
    int is_started = 0;
    int msg_count = 0;

    while (isRunning) {
        socklen_t addrlen = sizeof(addr);
        int receivedBytes = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr*)&addr, &addrlen);

        if (receivedBytes < 0) {
            if (errno == EINTR || !isRunning) {
                break;
            }
            ERR("recvfrom");
        }
        if (receivedBytes != 16) {
            fprintf(stderr, "Invalid datagram size!\n");
            continue;
        }

        message = (message_t*)buf;

        switch (message->type) {
            case 'l':
                pthread_mutex_lock(data->players_mutex);
                if (is_started) {
                    pthread_mutex_unlock(data->players_mutex);
                    continue;
                }

                int is_duplicate = 0;
                for (int i = 0; i < *data->players_count; i++) {
                    if (memcmp(&data->players[i].addr, &addr, sizeof(addr)) == 0) {
                        is_duplicate = 1;
                        break;
                    }
                }

                if (is_duplicate) {
                    pthread_mutex_unlock(data->players_mutex);
                    continue;
                }


                data->players[*data->players_count].addr = addr;
                strncpy(data->players[*data->players_count].name, message->message, MAX_NAME_LENGTH);
                data->players[*data->players_count].name[MAX_NAME_LENGTH] = '\0';
                data->players[*data->players_count].pebbles = STARTING_PEBBLES;

                (*data->players_count)++;

                if (*data->players_count >= MAX_CLIENTS) {
                    is_started = 1;
                }
                pthread_mutex_unlock(data->players_mutex);


                printf("Welcome, %14s\n", message->message);
                break;

            case 'c': {
                uint16_t* spell = (uint16_t*)message->message;
                uint16_t spell_id = ntohs(spell[0]);
                uint16_t x = ntohs(spell[1]);
                uint16_t y = ntohs(spell[2]);

                if (x >= BOARD_SIZE || y >= BOARD_SIZE || spell_id >= SPELL_TYPES) {
                    printf("Invalid spell data\n");
                    break;
                }

                //szukamy kto wyslal
                int sender_id = -1;
                pthread_mutex_lock(data->players_mutex);
                for (int i = 0; i < *data->players_count; i++) {
                    if (memcmp(&data->players[i].addr, &addr, sizeof(addr)) == 0) {
                        sender_id = i;
                        break;
                    }
                }
                pthread_mutex_unlock(data->players_mutex);


                if (sender_id == -1) break;

                pthread_mutex_lock(data->fifo_mutex);
                if (data->fifo.count < MAX_QUEUE) {
                    data->fifo.elements[data->fifo.rear].message = *message;
                    data->fifo.elements[data->fifo.rear].player_id = sender_id;
                    data->fifo.rear = (data->fifo.rear + 1) % MAX_QUEUE;
                    data->fifo.count++;
                    pthread_cond_signal(data->fifo_cond);
                }
                pthread_mutex_unlock(data->fifo_mutex);
                break;
            }
            case 'q':
                printf("Someone quit. Goodbye!\n");
                break;
            default:
                printf("Unknown message type!\n");
        }

        msg_count++;
        if (msg_count == 4) {
            break;
        }
    }
}

void* thread_work(void* arg) {
    server_data_t* data = (server_data_t*) arg;

    //kopia lokalna bardzo wazne!!!
    task_t local_task;

    while (isRunning) {
        pthread_mutex_lock(data->fifo_mutex);
        while (data->fifo.count == 0 && isRunning) {
            pthread_cond_wait(data->fifo_cond, data->fifo_mutex);
        }

        if (!isRunning) {
            pthread_mutex_unlock(data->fifo_mutex);
            break;
        }

        //kopia lokalna
        local_task = data->fifo.elements[data->fifo.front];
        data->fifo.front = (data->fifo.front + 1) % MAX_QUEUE;
        data->fifo.count--;

        pthread_mutex_unlock(data->fifo_mutex);

        //dekodowanie
        uint16_t* spell = (uint16_t*)local_task.message.message;
        uint16_t spell_id = ntohs(spell[0]);
        uint16_t x = ntohs(spell[1]);
        uint16_t y = ntohs(spell[2]);

        int cost = spell_costs[spell_id];

        pthread_mutex_lock(data->players_mutex);
        player_t* current_player = &data->players[local_task.player_id];

        if (current_player->pebbles >= cost) {
            current_player->pebbles -= cost;
            pthread_mutex_unlock(data->players_mutex);

            usleep(FAMILIAR_DELAY * 1000);

            printf("Someone casts %s onto %d,%d\n", spell_names[spell_id], x, y);
        } else {

            char name_copy[MAX_NAME_LENGTH + 1];
            strcpy(name_copy, current_player->name);
            pthread_mutex_unlock(data->players_mutex);

            usleep(FAMILIAR_DELAY * 1000);

            printf("[tee hee] Not enough pebbles, %s!\n", name_copy);
        }
    }
    return NULL;
}

int main(int argc, char** argv) {
    printf("sizeof(struct packed) == %d\n", (int)sizeof(packed_t));
    printf("sizeof(struct not_packed) == %d\n", (int)sizeof(not_packed_t));

    if (argc != 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    pthread_t threads[THREAD_COUNT];
    int fd;
    server_data_t data;

    pthread_mutex_t fifo_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t fifo_cond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t players_mutex = PTHREAD_MUTEX_INITIALIZER;
    int players_count = 0;

    data.fifo_mutex = &fifo_mutex;
    data.fifo_cond = &fifo_cond;
    data.players_mutex = &players_mutex;
    data.players_count = &players_count;

    data.fifo.front = 0;
    data.fifo.rear = 0;
    data.fifo.count = 0;

    sethandler(SIG_IGN, SIGPIPE);
    sethandler(sigint_handler, SIGINT);

    fd = bind_inet_socket(atoi(argv[1]), SOCK_DGRAM, BACKLOG);

    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_create(&threads[i], NULL, thread_work, &data);
    }

    doServer(fd, &data);

    //broadcast aby miec pewnosc ze zaden watek nie spi
    pthread_mutex_lock(&fifo_mutex);
    pthread_cond_broadcast(&fifo_cond);
    pthread_mutex_unlock(&fifo_mutex);

    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&fifo_mutex);
    pthread_cond_destroy(&fifo_cond);
    pthread_mutex_destroy(&players_mutex);
    close(fd);

    return EXIT_SUCCESS;
}