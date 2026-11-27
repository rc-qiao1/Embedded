#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(void)
{
    char buf;
    int fd;
    ssize_t ret;

    fd = open("/dev/cdev_demo", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    ret = read(fd, &buf, 1);

    if (ret < 0) {
        printf("read failed: %s\n", strerror(errno));
    } else if (ret == 0) {
        printf("read returned EOF\n");
    } else {
        printf("read %zd byte: %c\n", ret, buf);
    }

    close(fd);
    return 0;
}
