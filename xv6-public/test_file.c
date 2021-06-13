#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
int main(int argc, char *argv[])
{
    int fd, i;
    char path[] = "file0";
    char data[512];
    memset(data, 'a', sizeof data);

    // file0
    fd = open(path, O_CREATE | O_RDWR);

    write(fd, data, sizeof data);

    close(fd);

    // file1
    path[4] = '1';
    fd = open(path, O_CREATE | O_RDWR);

    write(fd, data, sizeof data);
    sync();

    close(fd);

    // file2
    path[4] = '2';
    fd = open(path, O_CREATE | O_RDWR);

    for (i = 0; i < 36864; ++i)
    {
        write(fd, data, sizeof data);
    }

    sync();

    close(fd);

    exit();
}
