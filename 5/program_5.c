#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>

const int CLIENTS_NUM = 55;

// Семафор для посетителей отеля
const char *clients_sem = "/clients-semaphore";
sem_t *clients;

// Общая память - список комнат
char rooms[] = "rooms";
int rooms_fd;
int double_rooms = 15;
int size = 25;

void init_memory(int shm) {
    char *addr;
    char buf[25];
    for (int i = 0; i < 25; ++i) {
        buf[i] = 0;
    }

    addr = mmap(0, size, PROT_WRITE|PROT_READ, MAP_SHARED, shm, 0);
    if (addr == (char*)-1 ) {
        printf("Error getting pointer to shared memory\n");
        return;
    }

    memcpy(addr, buf, size);
    close(shm);
}

int fork_() {
    int result = fork();
    if (result < 0) {
        printf("Error while forking\n");
        exit(-1);
    }
    return result;
}

// Функция осуществляющая при запуске общие манипуляции с памятью и семафорами
void init(void) {
    // Создание или открытие мьютекса для доступа к буферу (доступ открыт)
    int fd_clients = shm_open("clients_sem", O_RDWR | O_CREAT, 0666);
    ftruncate(fd_clients, sizeof(sem_t));
    clients = mmap(0, sizeof(sem_t), PROT_WRITE|PROT_READ, MAP_SHARED, fd_clients, 0);
    sem_init(clients, 1, 1);

    if ((rooms_fd = shm_open(rooms, O_CREAT|O_RDWR, 0666)) == -1 ) {
        printf("Opening error.\n");
        perror("shm_open");
        exit(-1);
    }

    if (ftruncate(rooms_fd, size) == -1) {
        printf("Truncating error.\n");
        perror("ftruncate");
        exit(-1);
    } else {
        init_memory(rooms_fd);
        printf("Hotel with %d rooms opened!\n", size);
    }

    close(rooms_fd);
}

void unlink_all(void) {
    if(shm_unlink(rooms) == -1) {
        printf("Shared memory is absent\n");
        perror("shm_unlink");
    }

    sem_destroy(clients);
}

int check_rooms(int gender, int num) {
    char *addr;
    int shm;
    if ((shm = shm_open(rooms, O_RDWR, 0666)) == -1 ) {
        printf("Opening error\n");
        perror("shm_open");
        return 1;
    }

    addr = mmap(0, size, PROT_WRITE|PROT_READ, MAP_SHARED, shm, 0);
    if (addr == (char*)-1 ) {
        printf("Error getting pointer to shared memory\n");
        return 1;
    }

    struct timespec time;
    time.tv_sec = 0;
    time.tv_nsec = 999999999 / (rand() % 10);
    nanosleep(&time, NULL);
    sleep(rand() % 3);

    printf("A %s #%d came to the hotel...\n", (gender == 2 ? "woman" : "man"), num);

    sem_wait(clients);

    char result = (char)gender;
    for (int i = 0; i < size; ++i) {
        if (i < double_rooms && (addr[i] == gender || addr[i] == 0)) {
            if (addr[i] == gender) {
                result *= 2;
            }
            addr[i] = result;
            close(shm);
            printf("A %s #%d entered double room #%d...\n", (gender == 2 ? "woman" : "man"), num, i + 1);
            sem_post(clients);
            return 0;
        }
        if (i >= double_rooms && addr[i] == 0) {
            addr[i] = result;
            close(shm);
            printf("A %s #%d entered single room #%d...\n", (gender == 2 ? "woman" : "man"), num, i + 1);
            sem_post(clients);
            return 0;
        }
    }

    sem_post(clients);

    printf("A %s #%d left hotel\n", (gender == 2 ? "woman" : "man"), num);
    close(shm);
    return 1;
}

int main() {
    init();

    int clients_num = 1;
    while (clients_num <= CLIENTS_NUM) {
        if (fork_() == 0) {
            srand(getpid() % 47);
            int gender = ((rand()) % 2) + 2; // 2 - женщина, 3 - мужчина
            int i = check_rooms(gender, getpid());
            exit(0);
        }
        ++clients_num;
    }

    fd_set rfds;
    struct timeval tv;
    int retval;
    FD_ZERO(&rfds);
    FD_SET(0, &rfds);
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    retval = select(1, &rfds, NULL, NULL, &tv);
    if (retval) {
        printf("\nKilling child processes.\n");
        signal(SIGQUIT, SIG_IGN);
        kill(0, SIGQUIT);
        printf("All child processes were killed.\n");
    } else {
        while(wait(NULL) > 0);
        printf("\nChild processes exited by their own.\n");
    }
    unlink_all();
    return 0;
}