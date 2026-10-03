#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <linux/videodev2.h>

#define DEV_NAME "/dev/video10"
#define OUT_FILE "frame1.jpg"
#define BUFFER_COUNT 4

struct buffer {
    void *start;        /* mmap 返回地址 */
    size_t length;      /* buffer 容量*/
};

int main(void)
{
    int fd = -1;
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;
    struct buffer *buffers = NULL;
    enum v4l2_buf_type type;
    FILE *fp = NULL;
    int i;
    int ret = -1;

    /*============== 1. Open 设备 ==============*/
    fd = open(DEV_NAME, O_RDWR);
    if (fd < 0) {
        perror("open" DEV_NAME);
        return 1;
    }
    printf("[1] open %s success, fd = %d\n", DEV_NAME, fd);

    /*============== 2. VIDIOC_QUERYCAP: 查询设备能力，确认它是视频采集设备，且支持 streaming I/O ==============*/
    memset(&cap, 0, sizeof(cap));
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        goto cleanup;
    }
    printf("[2] QUERYCAP 成功: driver=%s, card=%s\n", cap.driver, cap.card);

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

    /*====================== 9. VIDIOC_DQBUF: 取出一帧（会阻塞直到有数据） ==============*/
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("VIDIOC_DQBUF");
        goto cleanup;
    }
    printf("[9] DQBUF: 取出一帧成功, index=%u, bytesused=%u\n", 
        buf.index, buf.bytesused);
    
    /*====================== 10. 把数据写进 frame.jpg ==============*/
    fp = fopen(OUT_FILE, "wb");
    if (!fp) {
        perror("fopen " OUT_FILE);
        goto requeue;
    }
    size_t written = fwrite(buffers[buf.index].start, 1, buf.bytesused, fp);
    fclose(fp);
    fp = NULL;

    if(written != buf.bytesused) {
        fprintf(stderr, "警告: 只写入 %zu / %u 字节\n", written, buf.bytesused);
    }else{
        printf("[10] 写入 %s (%u字节)\n", OUT_FILE, buf.bytesused);
    }


    /*=================== 11. VIDIOC_QBUF：把 buffer 还回去===========*/
requeue:
    if(ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        perror("VIDIOC_QBUF (requeue)");
        goto streamoff;
    }
    printf("[11] buffer[%d] 还回队列成功\n", buf.index);

    /*=================== 12. VIDIOC_STREAMOFF: 停止采集===========*/
streamoff:
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0)
    {
        perror("VIDIOC_STREAMOFF");
    }else{
        printf("[12] STREAMOFF: 停止采集\n");   
    }
    ret = 0;

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
