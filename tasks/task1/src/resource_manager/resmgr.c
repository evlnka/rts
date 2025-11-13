/*
 *  Менеджер ресурсов (Linux версия)
 *  
 *  Реализован простой менеджер ресурсов с поддержкой:
 *  - Многопоточное обслуживание клиентов
 *  - Буфер устройства с операциями чтения/записи
 *  - Команды управления (очистка буфера, получение статуса)
 *  - Симуляция прав доступа
 *  - Ведение статистики операций
 */

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define EXAMPLE_SOCK_PATH "/tmp/example_resmgr.sock"
#define DEVICE_BUFFER_SIZE 1024
#define MAX_CLIENTS 10

static const char *progname = "resmgr";
static int optv = 0;
static int listen_fd = -1;

// Структура для хранения состояния устройства
typedef struct {
    char buffer[DEVICE_BUFFER_SIZE];
    size_t buffer_size;
    size_t read_pos;
    size_t write_pos;
    int access_level; // 0 - read-only, 1 - read-write
    unsigned long read_count;
    unsigned long write_count;
    pthread_mutex_t mutex;
} device_t;

static device_t device;

// Прототипы функций
static void options(int argc, char *argv[]);
static void install_signals(void);
static void on_signal(int signo);
static void *client_thread(void *arg);
static void device_init(void);
static int handle_command(int client_fd, const char *cmd, size_t cmd_len);
static void send_response(int client_fd, const char *response);

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("%s: starting...\n", progname);
    options(argc, argv);
    install_signals();
    
    // Инициализация устройства
    device_init();

    // Создаём UNIX-сокет и биндимся на путь
    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, EXAMPLE_SOCK_PATH, sizeof(addr.sun_path) - 1);

    unlink(EXAMPLE_SOCK_PATH);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (listen(listen_fd, MAX_CLIENTS) == -1) {
        perror("listen");
        close(listen_fd);
        unlink(EXAMPLE_SOCK_PATH);
        return EXIT_FAILURE;
    }

    printf("%s: listening on %s\n", progname, EXAMPLE_SOCK_PATH);
    printf("Используйте клиент для отправки команд:\n");
    printf("  DATA <text> - запись в устройство\n");
    printf("  READ - чтение из устройства\n");
    printf("  CLEAR - очистка буфера\n");
    printf("  STATUS - получение статистики\n");
    printf("  HELP - справка по командам\n");

    while (1) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd == -1) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        if (optv) {
            printf("%s: новое подключение (fd=%d)\n", progname, client_fd);
        }

        pthread_t th;
        if (pthread_create(&th, NULL, client_thread, (void *)(long)client_fd) != 0) {
            perror("pthread_create");
            close(client_fd);
            continue;
        }
        pthread_detach(th);
    }

    if (listen_fd != -1) close(listen_fd);
    unlink(EXAMPLE_SOCK_PATH);
    pthread_mutex_destroy(&device.mutex);
    return EXIT_SUCCESS;
}

// Инициализация устройства
static void device_init(void)
{
    memset(&device, 0, sizeof(device));
    device.access_level = 1; // read-write по умолчанию
    pthread_mutex_init(&device.mutex, NULL);
    strcpy(device.buffer, "Добро пожаловать в менеджер ресурсов!");
    device.buffer_size = strlen(device.buffer);
}

// Обработка команд клиента
static int handle_command(int client_fd, const char *cmd, size_t cmd_len)
{
    char command[64];
    char argument[DEVICE_BUFFER_SIZE];
    
    // Парсинг команды
    if (sscanf(cmd, "%63s %1023[^\n]", command, argument) < 1) {
        send_response(client_fd, "ERROR: Неверный формат команды");
        return -1;
    }

    pthread_mutex_lock(&device.mutex);

    if (strcmp(command, "READ") == 0) {
        // Чтение из буфера устройства
        device.read_count++;
        if (device.buffer_size == 0) {
            send_response(client_fd, "BUFFER_EMPTY");
        } else {
            char response[DEVICE_BUFFER_SIZE + 64];
            snprintf(response, sizeof(response), "DATA: %.*s", 
                    (int)device.buffer_size, device.buffer);
            send_response(client_fd, response);
        }
    }
    else if (strcmp(command, "DATA") == 0) {
        // Запись в буфер устройства
        if (device.access_level == 0) {
            send_response(client_fd, "ERROR: Устройство доступно только для чтения");
        } else {
            size_t len = strlen(argument);
            if (len >= DEVICE_BUFFER_SIZE) {
                len = DEVICE_BUFFER_SIZE - 1;
            }
            memcpy(device.buffer, argument, len);
            device.buffer[len] = '\0';
            device.buffer_size = len;
            device.write_count++;
            
            char response[64];
            snprintf(response, sizeof(response), "WRITTEN: %zu bytes", len);
            send_response(client_fd, response);
        }
    }
    else if (strcmp(command, "CLEAR") == 0) {
        // Очистка буфера
        if (device.access_level == 0) {
            send_response(client_fd, "ERROR: Устройство доступно только для чтения");
        } else {
            device.buffer[0] = '\0';
            device.buffer_size = 0;
            device.read_pos = 0;
            device.write_pos = 0;
            send_response(client_fd, "BUFFER_CLEARED");
        }
    }
    else if (strcmp(command, "STATUS") == 0) {
        // Получение статистики
        char status[256];
        snprintf(status, sizeof(status),
                "STATUS: buffer_size=%zu, reads=%lu, writes=%lu, access=%s",
                device.buffer_size, device.read_count, device.write_count,
                device.access_level ? "read-write" : "read-only");
        send_response(client_fd, status);
    }
    else if (strcmp(command, "SET_ACCESS") == 0) {
        // Установка уровня доступа
        if (strcmp(argument, "read-only") == 0) {
            device.access_level = 0;
            send_response(client_fd, "ACCESS_SET: read-only");
        } else if (strcmp(argument, "read-write") == 0) {
            device.access_level = 1;
            send_response(client_fd, "ACCESS_SET: read-write");
        } else {
            send_response(client_fd, "ERROR: Неверный уровень доступа (read-only/read-write)");
        }
    }
    else if (strcmp(command, "HELP") == 0) {
        // Справка по командам
        send_response(client_fd, 
            "Доступные команды:\n"
            "READ - чтение данных\n"
            "DATA <text> - запись данных\n" 
            "CLEAR - очистка буфера\n"
            "STATUS - статистика устройства\n"
            "SET_ACCESS <read-only|read-write> - установка уровня доступа\n"
            "HELP - эта справка");
    }
    else {
        send_response(client_fd, "ERROR: Неизвестная команда. Используйте HELP для справки.");
    }

    pthread_mutex_unlock(&device.mutex);
    return 0;
}

// Отправка ответа клиенту
static void send_response(int client_fd, const char *response)
{
    size_t len = strlen(response);
    if (send(client_fd, response, len, 0) != (ssize_t)len) {
        perror("send response");
    }
}

// Поток обработки клиента
static void *client_thread(void *arg)
{
    int fd = (int)(long)arg;
    char buf[1024];

    // Приветственное сообщение
    send_response(fd, "Подключение к менеджеру ресурсов установлено. Используйте HELP для справки.");

    for (;;) {
        ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n == 0) {
            if (optv) printf("%s: клиент отключился (fd=%d)\n", progname, fd);
            break;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recv");
            break;
        }
        
        buf[n] = '\0';
        
        // Удаляем символы новой строки
        if (buf[n-1] == '\n') buf[n-1] = '\0';
        if (buf[n-2] == '\r') buf[n-2] = '\0';

        if (optv) {
            printf("%s: получена команда: %s\n", progname, buf);
        }

        handle_command(fd, buf, n);
    }

    close(fd);
    return NULL;
}

static void options(int argc, char *argv[])
{
    int opt;
    optv = 0;
    while ((opt = getopt(argc, argv, "v")) != -1) {
        switch (opt) {
            case 'v':
                optv++;
                break;
        }
    }
}

static void install_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL); // Игнорировать SIGPIPE
}

static void on_signal(int signo)
{
    (void)signo;
    if (listen_fd != -1) close(listen_fd);
    unlink(EXAMPLE_SOCK_PATH);
    fprintf(stderr, "\n%s: завершение работы\n", progname);
    _exit(0);
}