#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define EXAMPLE_SOCK_PATH "/tmp/example_resmgr.sock"
#define BUFFER_SIZE 1024

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Использование: %s <команда> [аргументы]\n", argv[0]);
        fprintf(stderr, "Примеры:\n");
        fprintf(stderr, "  %s \"HELP\"\n", argv[0]);
        fprintf(stderr, "  %s \"READ\"\n", argv[0]);
        fprintf(stderr, "  %s \"DATA Привет мир!\"\n", argv[0]);
        fprintf(stderr, "  %s \"STATUS\"\n", argv[0]);
        fprintf(stderr, "  %s \"CLEAR\"\n", argv[0]);
        return EXIT_FAILURE;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, EXAMPLE_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("connect");
        close(fd);
        return EXIT_FAILURE;
    }

    // Получаем приветственное сообщение
    char buf[BUFFER_SIZE];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        printf("Сервер: %s\n", buf);
    }

    // Отправляем команду
    const char *cmd = argv[1];
    if (send(fd, cmd, strlen(cmd), 0) == -1) {
        perror("send");
        close(fd);
        return EXIT_FAILURE;
    }

    // Получаем ответ на команду (блокирующее чтение)
    n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        printf("Ответ: %s\n", buf);
    } else if (n == 0) {
        printf("Сервер закрыл соединение\n");
    } else {
        perror("recv");
    }

    close(fd);
    return EXIT_SUCCESS;
}