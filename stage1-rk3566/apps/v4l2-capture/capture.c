#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <linux/videodev2.h>
#include <time.h>

#define OUT_FILE "frame1.jpg"
#define BUFFER_COUNT 4
#define WARMUP_FRAMES   5   /* 预热帧数，不计入FPS*/

struct buffer {
    void *start;        /* mmap 返回地址 */
    size_t length;      /* buffer 容量*/
};

/* ------------- 及时辅助---------------*/
static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* 自动查找USB摄像头采集节点*/
static int find_capture_device(char *dev_path, size_t path_len)
{
    DIR *dir;
    struct dirent *entry;
    char full_path[300];
    int fd = -1;

    dir = opendir("/dev");
    if (!dir) {
        perror("opendir /dev");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL){
        /* 只关心 video* 节点*/
        if (strncmp(entry->d_name, "video", 5) != 0)
            continue;
        
        snprintf(full_path, sizeof(full_path), "/dev/%s", entry->d_name);

        fd = open(full_path, O_RDWR);
        if(fd < 0){
            if (errno == EACCES || errno == EPERM) {
                /* 没权限访问，跳过 */
                fprintf(stderr, "权限不足，无法打开 %s: %s\n", full_path, strerror(errno));
            } 
            /* 其他错误（如设备不存在）忽略，继续找下一个*/
            continue;
        }

        struct v4l2_capability cap;
        if(ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0){
            close(fd);
            fd = -1;
            continue;
        }

        unsigned int caps = cap.device_caps ? cap.device_caps : cap.capabilities;

        /* 必须是视频采集设备且支持 streaming I/O*/
        if(!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING)){
            close(fd);
            fd = -1;
            continue;
        }

        /* 只认uvcvideo 驱动，避免误选 RKISP 等平台节点*/
        if(strcmp((const char *)cap.driver, "uvcvideo") != 0){
            close(fd);
            fd = -1;
            continue;
        }

        /* 总线信息应包含 “usb”, 进一步确认是USB摄像头*/
        if(strstr((char *)cap.bus_info, "usb") == NULL){
            close(fd);
            fd = -1;
            continue;
        }

        /* 找到符合条件的节点 */
        snprintf(dev_path, path_len, "%.*s", (int)(path_len - 1), full_path);
        closedir(dir);
        return fd;   /* 直接返回已打开的 fd */
    }

    closedir(dir);
    return -1;

}

int main(int argc, char **argv)
{
    int fd = -1;
    char dev_path[300] = {0};
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;
    struct buffer *buffers = NULL;
    enum v4l2_buf_type type;
    FILE *fp = NULL;
    int i;
    int ret = -1;
    int streamed = 0;   /* ← 新增：是否已经 STREAMON */

    /* ---------------- 解析命令行参数-------------------------*/
    int total_frames = 30;
    if (argc >= 2){
        total_frames = atoi(argv[1]);
        if(total_frames <= 0){
            fprintf(stderr, "参数错误: total_frames 必须大于0\n");
            return 1;
        }
    }
    printf("采集总帧数: %d\n", total_frames);
    /*================= 0. 查找USB摄像头采集节点 ==============*/
    fd = find_capture_device(dev_path, sizeof(dev_path));
    if (fd < 0) {
        fprintf(stderr, "未找到符合条件的USB摄像头采集节点\n");
        return 1;
    }
    printf("[0] 找到采集设备: %s, fd=%d\n", dev_path, fd);

    // /*============== 1. Open 设备 ==============*/
    // fd = open(DEV_NAME, O_RDWR);
    // if (fd < 0) {
    //     perror("open" DEV_NAME);
    //     return 1;
    // }
    // printf("[1] open %s success, fd = %d\n", DEV_NAME, fd);

    // /*============== 2. VIDIOC_QUERYCAP: 查询设备能力，确认它是视频采集设备，且支持 streaming I/O ==============*/
    // memset(&cap, 0, sizeof(cap));
    // if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
    //     perror("VIDIOC_QUERYCAP");
    //     goto cleanup;
    // }
    // printf("[2] QUERYCAP 成功: driver=%s, card=%s\n", cap.driver, cap.card);

    /*============== 3. VIDIOC_S_FMT: 请求MJPPG 640X480 注：这只是请求，驱动可以给你别的格式 ==============*/
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        goto cleanup;
    }
    printf("[3] S_FMT 请求: 640x480 MJPG\n");

    /*=============== 4. VIDIOC_G_FMT: 读回实际生效的格式（经典陷阱！）后面算buffer大小必须用这里的值*/
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
        perror("VIDIOC_G_FMT");
        goto cleanup;
    }

    char fourcc[5] = {
        fmt.fmt.pix.pixelformat & 0xFF,
        (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
        (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
        (fmt.fmt.pix.pixelformat >> 24) & 0xFF,
        0
    };
    printf("[4] G_FMT 生效: width=%d, height=%d, pixelformat=%s\n", 
        fmt.fmt.pix.width, fmt.fmt.pix.height, fourcc);
    /* 检查驱动是否真的给了MJPG （可能被换为YUYV）*/
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
        fprintf(stderr, "驱动没有给MJPG, 而是给了 %s\n", fourcc);
        goto cleanup;
    }

    /*========================= 5. VIDIOC_REQBUFS: 请求buffer, MMAP方式 ==============*/
    memset(&req, 0, sizeof(req));
    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        goto cleanup;
    }
    if(req.count < 2){
        fprintf(stderr, "驱动只给了 %d 个buffer, 不够用\n", req.count);
        goto cleanup;
    }
    printf("[5] REQBUFS 成功, 驱动给了 %d 个buffer\n", req.count);

    buffers = calloc(req.count, sizeof(*buffers));
    if (!buffers) {
        perror("calloc buffers");
        goto cleanup;
    }

    /*========================= 6. VIDIOC_QUERYBUF: 查询每个buffer的物理地址  拿length/offset, 然后 mmap==============*/
    for (i = 0; i < (int)req.count; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            goto cleanup;
        }

        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) {
            perror("mmap");
            goto cleanup;
        }

        printf("[6] buffer[%d]: length=%u, offset=%u mmap=%p\n", 
            i, buf.length, buf.m.offset, buffers[i].start);
    }

    /*========================= 7. 循环4次：VIDIOC_QBUF: 把buffer入队列 ==============*/
    for (i = 0; i < (int)req.count; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            goto cleanup;
        }
        printf("[7] buffer[%d] 入队列成功\n", i);
    }

    /*====================== 8. VIDIOC_STREAMON: 开始采集 ==============*/
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0)
    {
        perror("VIDIOC_STREAMON");
        goto cleanup;
    }
    printf("[8] STREAMON: 开始采集\n");
    streamed = 1;

    /*===================== 丢弃前 WARMUP_FRAMES 帧，等待自动曝光稳定===============*/
    for(i = 0; i < WARMUP_FRAMES; i++){
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            perror("VIDIOC_DQBUF (warmup)");
            goto streamoff;  /* 出错直接跳到停止采集 */
        }
        if(ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF (warmup)");
            goto streamoff;
        }
    }
    printf("预热 %d 帧完成，开始正式计时\n", WARMUP_FRAMES);


    /*====================== 9. VIDIOC_DQBUF: 连续抓帧 + 计时 ==============*/
    double t_start = now_sec();
    int frames_ok = 0;
    long long total_bytes = 0;

    for(i = 0; i < total_frames; i++){
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            perror("VIDIOC_DQBUF");
            goto streamoff;  /* 出错直接跳到停止采集 */
        }
        frames_ok++;
        total_bytes += buf.bytesused;

        /* 只保存第一帧，避免磁盘IO拖慢 FPS*/
        if(i == 0){
            FILE *fp = fopen(OUT_FILE, "wb");
            if (fp) {
                fwrite(buffers[buf.index].start, 1, buf.bytesused, fp);
                fclose(fp);
                printf("保存第一帧到 %s (%u字节)\n", OUT_FILE, buf.bytesused);
            } else {
                perror("fopen " OUT_FILE);
            }
        }
        
        /* 打印每一帧的进度*/
        if((i + 1) % 10 == 0 || i == total_frames - 1){
           printf(" 已抓 %d %d 帧，本帧 %u 字节\n", i + 1, total_frames, buf.bytesused);
        }

        if(ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            goto streamoff;
        }
    }
   
    double t_end = now_sec();
   
    /* ==================== 统计结果 ========================== */
    double elapsed = t_end - t_start;
    printf("\n========== 结果 ==========\n");
    printf("成功抓取帧数 : %d\n", frames_ok);
    printf("总耗时       : %.3f 秒\n", elapsed);
    if (elapsed > 0) {
        printf("平均 FPS     : %.2f\n", frames_ok / elapsed);
    }
    if (frames_ok > 0) {
        printf("平均每帧大小 : %.1f 字节\n",
               (double)total_bytes / frames_ok);
    }
    printf("==========================\n");

    ret = 0;

    /*=================== 12. VIDIOC_STREAMOFF: 停止采集===========*/
streamoff:
    if (streamed) {                     /* ← 用 streamed 判断是否执行 STREAMOFF */
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0)
            perror("VIDIOC_STREAMOFF");
        else
            printf("[12] STREAMOFF: 停止采集\n");
    }

    /*================= 13. munmap X N, close, 释放内存================*/

cleanup:
    if (buffers){
        for(i = 0; i < (int)req.count; i++){
            if (buffers[i].start && buffers[i].start != MAP_FAILED)
                munmap(buffers[i].start, buffers[i].length);
        }
        free(buffers);
    }
    if(fp) fclose(fp);
    if(fd >= 0) close(fd);
    printf("[13] 清理完成\n");
    return ret == 0 ? 0 : 1;


}
