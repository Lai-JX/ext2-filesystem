#include "../include/ddriver.h"
#include <linux/fs.h>

int main(int argc, char const *argv[])
{
    int size;
    struct ddriver_state state;
    int fd = ddriver_open("/home/students/200110515/ddriver");   // 打开ddriver设备,应该是绝对路径？
    if (fd < 0) {
        return -1;
    }
    /* Cycle 1: read/write test */
    char buffer[512]={'a'};
    char rbuffer[512];
    buffer[511] = '\0';
    if (fd < 0){
        return fd;
    }
    ddriver_write(fd, buffer, 512);     // 写入设备
    ddriver_seek(fd, 0, SEEK_SET);      // 移动磁盘头
    ddriver_read(fd, rbuffer, 512);     // 从设备读取
    printf("%s\n", rbuffer);

    /* Cycle 2: ioctl test - return int */
    ddriver_ioctl(fd, IOC_REQ_DEVICE_SIZE, &size);  // I/O控制
    printf("%d\n", size);

    /* Cycle 3: ioctl test - return struct */
    ddriver_ioctl(fd, IOC_REQ_DEVICE_STATE, &state);
    printf("read_cnt: %d\n", state.read_cnt);
    printf("write_cnt: %d\n", state.write_cnt);
    printf("seek_cnt: %d\n", state.seek_cnt);

    /* Cycle 4: ioctl test - re-init device */
    ddriver_ioctl(fd, IOC_REQ_DEVICE_RESET, &size);

    ddriver_ioctl(fd, IOC_REQ_DEVICE_SIZE, &size);
    printf("%d\n", size);

    ddriver_ioctl(fd, IOC_REQ_DEVICE_STATE, &state);
    printf("read_cnt: %d\n", state.read_cnt);
    printf("write_cnt: %d\n", state.write_cnt);
    printf("seek_cnt: %d\n", state.seek_cnt);

    ddriver_close(fd);

    printf("Test Pass :)\n");
    return 0;
}
