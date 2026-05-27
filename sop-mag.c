#include "l8_common.h"

#define SPELL_TYPES 3
const char* spell_names[SPELL_TYPES] = {"Divination", "Summon Elemental", "Fireball"};
#define BOARD_SIZE 8
#define BACKLOG 16

#define MAX_QUEUE 10
#define THREAD_COUNT 3
#define FAMILIAR_DELAY 100

#define MAX_CLIENTS 2
#define MAX_NAME_LENGTH 14

volatile sig_atomic_t isRunning = 1;

void sigint_handler() {
    isRunning = 0;
}

typedef struct __attribute__((__packed__)) packed
{
    char c1;
    int i1;
    char c2;
    int i2;
};
typedef struct not_packed
{
    char c1;
    int i1;
    char c2;
    int i2;
};


typedef struct __attribute__((__packed__)) message{
    char type;
    char padding;
    char message[MAX_NAME_LENGTH];
} message_t;
typedef struct fifo {
    message_t elements[MAX_QUEUE];
    int front;
    int rear;
    int count;
}fifo_t;
typedef struct server_data {
    fifo_t fifo;
    pthread_mutex_t* fifo_mutex;

    pthread_cond_t* fifo_cond;
}server_data_t;

void usage(char* name)
{
    printf("%s <in_port>\n", name);
    printf("  in_port - port that accepts messages\n");
    exit(EXIT_FAILURE);
}



void doServer(int fd, server_data_t* data) {
    struct sockaddr_in addr;

    message_t* message;
    char buf[MAX_NAME_LENGTH +2];

    int msg_count = 0;

    while (isRunning) {
        socklen_t addrlen = sizeof(addr);

        int receivedBytes = recvfrom(fd, buf, sizeof(buf),0,(struct sockaddr*)&addr, &addrlen);

        if (receivedBytes < 0) {
            if (errno == EINTR || !isRunning) {
                break;
            }
            ERR("recvfrom");
        }
        if (receivedBytes != 16) {
            fprintf(stderr,"Invalid datagram size!\n");
            continue;
        }

        message = (message_t*)buf;

        switch (message->type) {
            case 'l':
                printf("Welcome, %s\n", message->message);
                break;
            case 'c':
                uint16_t* spell = (uint16_t*)message->message;

                uint16_t spell_id = ntohs(spell[0]);

                uint16_t x = ntohs(spell[1]);

                uint16_t y = ntohs(spell[2]);

                if (x>=BOARD_SIZE || y>=BOARD_SIZE || spell_id>=SPELL_TYPES) {
                    printf("Invalid spell data\n");
                    break;
                }

                pthread_mutex_lock(data->fifo_mutex);
                if (data->fifo.count < MAX_QUEUE) {
                    data->fifo.elements[data->fifo.rear] = *message;

                    data->fifo.rear = (data->fifo.rear + 1) % MAX_QUEUE;

                    data->fifo.count++;
                    pthread_cond_signal(data->fifo_cond);

                }

                pthread_mutex_unlock(data->fifo_mutex);
                break;

            case 'q':
                printf("Someone quit. Goodbye!\n");
                break;
            default:
                printf("Unknown message type!\n");
        }

        msg_count++;

        if (msg_count ==4) {
            break;
        }
    }


}

void* thread_work(void* arg) {

    server_data_t* data = (server_data_t*) arg;

    struct sockaddr_in addr;

    message_t local_msg;
    while (isRunning) {
        pthread_mutex_lock(data->fifo_mutex);
        while (data->fifo.count == 0 && isRunning) {
            pthread_cond_wait(data->fifo_cond, data->fifo_mutex);
        }

        if (!isRunning) {
            pthread_mutex_unlock(data->fifo_mutex);
            break;
        }
        local_msg = data->fifo.elements[data->fifo.front];

        data->fifo.front = (data->fifo.front + 1) % MAX_QUEUE;

        data->fifo.count--;

        pthread_mutex_unlock(data->fifo_mutex);

        uint16_t* spell = (uint16_t*)local_msg.message;

        uint16_t spell_id = ntohs(spell[0]);

        uint16_t x = ntohs(spell[1]);

        uint16_t y = ntohs(spell[2]);

        ms_sleep(FAMILIAR_DELAY);

        printf("Someone casts %s onto %d,%d\n", spell_names[spell_id], x,y);
    }

    return NULL;
}

int main(int argc, char** argv)
{
    printf("sizeof(struct packed) == %d\n", sizeof(struct packed));
    printf("sizeof(struct not_packed) == %d\n", sizeof(struct not_packed));

    if (argc!=2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    pthread_t threads[THREAD_COUNT];

    int fd;

    server_data_t data;

    pthread_mutex_t fifo_mutex = PTHREAD_MUTEX_INITIALIZER;

    pthread_cond_t fifo_cond = PTHREAD_COND_INITIALIZER;

    data.fifo_mutex = &fifo_mutex;

    data.fifo_cond = &fifo_cond;

    data.fifo.front = 0;

    data.fifo.rear = 0;

    data.fifo.count = 0;

    sethandler(SIG_IGN, SIGPIPE);

    sethandler(sigint_handler, SIGINT);

    fd = bind_inet_socket(atoi(argv[1]), SOCK_DGRAM, BACKLOG);



    for (int i =0; i<THREAD_COUNT; i++) {
        pthread_create(&threads[i], NULL, thread_work, &data);
    }

    doServer(fd, &data);

    pthread_mutex_lock(&fifo_mutex);

    pthread_cond_broadcast(&fifo_cond);

    pthread_mutex_unlock(&fifo_mutex);

    for (int i =0; i< THREAD_COUNT; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&fifo_mutex);
    pthread_cond_destroy(&fifo_cond);
    close(fd);

    return EXIT_SUCCESS;


}
