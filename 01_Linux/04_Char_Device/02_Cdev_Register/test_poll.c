#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>

int main(void)
{
    int fd;
    int ret;
    char buf;
    struct pollfd pfd;

    fd = open("/dev/cdev_demo", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    printf("Waiting for data...\n");

    ret = poll(&pfd, 1, 5000);

    if (ret < 0) {
        perror("poll");
    } else if (ret == 0) {
        printf("poll timeout: no data\n");
    } else if (pfd.revents & POLLIN) {
        printf("device is readable\n");

        ret = read(fd, &buf, 1);
        if (ret > 0)
            printf("received: %c\n", buf);
        else if (ret < 0)
            perror("read");
    }

    close(fd);
    return 0;
}
