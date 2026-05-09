/*
 * VNC Stream Agent - Linux Desktop Streaming with Input Injection
 * Uses: X11/XShm for capture, libjpeg for encoding, XTest for input injection
 *
 * Build: gcc -O2 -o vnc_stream vnc_stream_linux.c -lX11 -lXext -lXfixes -lXtst -ljpeg -lpthread
 * Usage: ./vnc_stream [--port PORT] [--quality Q]
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XTest.h>
#include <sys/shm.h>
#include <jpeglib.h>

#define DEFAULT_PORT      40484
#define DEFAULT_QUALITY   75
#define MAX_JPEG_SIZE     (10 * 1024 * 1024)
#define HTTP_BUF_SIZE     65536
#define BOUNDARY          "frame"

typedef struct {
    Display*      display;
    Window        root;
    int           width;
    int           height;

    XShmSegmentInfo shm_info;
    XImage*        ximage;

    unsigned char* frame_buffer;
    int            frame_size;
    int            frame_capacity;
    pthread_mutex_t frame_lock;

    int            jpeg_quality;
    volatile int   running;
    volatile int   stop_requested;

    unsigned short port;
} VncStream;

/* ---- JPEG encode BGRA -> JPEG ---- */
static int encode_jpeg(VncStream* v, unsigned char** out_data, int* out_size)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    unsigned char* jpeg_buf = NULL;
    unsigned long jpeg_len = 0;
    jpeg_mem_dest(&cinfo, &jpeg_buf, &jpeg_len);

    cinfo.image_width = v->width;
    cinfo.image_height = v->height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, v->jpeg_quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    unsigned char* row = (unsigned char*)malloc(v->width * 3);
    if (!row) { jpeg_destroy_compress(&cinfo); free(jpeg_buf); return -1; }

    JSAMPROW row_ptr[1] = { row };
    while (cinfo.next_scanline < cinfo.image_height) {
        unsigned char* src = v->ximage->data + cinfo.next_scanline * v->ximage->bytes_per_line;
        for (int x = 0; x < v->width; x++) {
            row[x*3+0] = src[x*4+2]; /* R */
            row[x*3+1] = src[x*4+1]; /* G */
            row[x*3+2] = src[x*4+0]; /* B */
        }
        jpeg_write_scanlines(&cinfo, row_ptr, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    free(row);

    if (jpeg_len > 0 && jpeg_len < MAX_JPEG_SIZE && jpeg_buf) {
        *out_data = jpeg_buf;
        *out_size = (int)jpeg_len;
        return 1;
    }
    free(jpeg_buf);
    return 0;
}

/* ---- Capture Thread ---- */
static void* capture_thread(void* param)
{
    VncStream* v = (VncStream*)param;
    while (v->running && !v->stop_requested) {
        XShmGetImage(v->display, v->root, v->ximage, 0, 0, AllPlanes);

        /* Cursor overlay */
        XFixesCursorImage* cursor = XFixesGetCursorImage(v->display);
        if (cursor) {
            int cx = cursor->x - cursor->xhot;
            int cy = cursor->y - cursor->yhot;
            for (int y = 0; y < (int)cursor->height; y++) {
                int fy = cy + y;
                if (fy < 0 || fy >= v->height) continue;
                for (int x = 0; x < (int)cursor->width; x++) {
                    int fx = cx + x;
                    if (fx < 0 || fx >= v->width) continue;
                    unsigned long p = cursor->pixels[y * cursor->width + x];
                    int a = (p >> 24) & 0xFF;
                    if (a == 0) continue;
                    int di = fy * v->ximage->bytes_per_line + fx * 4;
                    float fa = a / 255.0f;
                    v->ximage->data[di+2] = (unsigned char)(((p>>16)&0xFF)*fa + v->ximage->data[di+2]*(1-fa));
                    v->ximage->data[di+1] = (unsigned char)(((p>>8)&0xFF)*fa + v->ximage->data[di+1]*(1-fa));
                    v->ximage->data[di+0] = (unsigned char)((p&0xFF)*fa + v->ximage->data[di+0]*(1-fa));
                }
            }
            XFree(cursor);
        }

        unsigned char* jpeg_data = NULL;
        int jpeg_size = 0;
        if (encode_jpeg(v, &jpeg_data, &jpeg_size) > 0 && jpeg_data) {
            pthread_mutex_lock(&v->frame_lock);
            if (jpeg_size <= v->frame_capacity) {
                memcpy(v->frame_buffer, jpeg_data, jpeg_size);
                v->frame_size = jpeg_size;
            }
            pthread_mutex_unlock(&v->frame_lock);
            free(jpeg_data);
        }
        usleep(50000);
    }
    return NULL;
}

/* ---- Input Injection ---- */
static void inject_mouse(int x, int y, int button, int action, int wheel, Display* d, int w, int h)
{
    XTestFakeMotionEvent(d, -1, x * w / 65535, y * h / 65535, CurrentTime);
    if (wheel) {
        int b = wheel > 0 ? 4 : 5;
        XTestFakeButtonEvent(d, b, True, CurrentTime);
        XTestFakeButtonEvent(d, b, False, CurrentTime);
    }
    if (action == 0 || action == 1) {
        int xb = (button == 1) ? 3 : (button == 2) ? 2 : 1;
        XTestFakeButtonEvent(d, xb, action == 0 ? True : False, CurrentTime);
    }
    XFlush(d);
}

static void inject_keyboard(int vk_code, int action, Display* d)
{
    XTestFakeKeyEvent(d, vk_code, action == 0 ? True : False, CurrentTime);
    XFlush(d);
}

/* ---- HTTP Helpers ---- */
static void send_http(int s, const char* status, const char* ct, const unsigned char* body, int blen)
{
    char hdr[1024];
    int hl = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", status, ct, blen);
    send(s, hdr, hl, MSG_NOSIGNAL);
    if (body && blen > 0) send(s, body, blen, MSG_NOSIGNAL);
}

static void send_frame(int s, const unsigned char* jpeg, int size)
{
    char ph[256];
    int pl = snprintf(ph, sizeof(ph), "--" BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n", size);
    send(s, ph, pl, MSG_NOSIGNAL);
    send(s, jpeg, size, MSG_NOSIGNAL);
    send(s, "\r\n", 2, MSG_NOSIGNAL);
}

static int json_int(const char* json, const char* key, int def)
{
    const char* p = strstr(json, key);
    if (!p) return def;
    p += strlen(key);
    while (*p == ':' || *p == ' ' || *p == '\t') p++;
    return atoi(p);
}

static void parse_http(const char* data, char* method, char* path, char** body, int* blen)
{
    const char* s1 = strchr(data, ' ');
    const char* s2 = s1 ? strchr(s1 + 1, ' ') : NULL;
    if (!s1 || !s2) return;
    int ml = (int)(s1 - data); if (ml >= 15) ml = 14;
    memcpy(method, data, ml);
    int pl = (int)(s2 - s1 - 1); if (pl >= 254) pl = 253;
    memcpy(path, s1 + 1, pl);
    const char* bs = strstr(data, "\r\n\r\n");
    if (bs) { *body = (char*)bs + 4; *blen = (int)(data + strlen(data) - *body); if (*blen < 0) *blen = 0; }
}

static void handle_client(int client, VncStream* v)
{
    char buf[HTTP_BUF_SIZE];
    int rlen = recv(client, buf, sizeof(buf) - 1, 0);
    if (rlen <= 0) { close(client); return; }
    buf[rlen] = '\0';

    char method[16] = {0}, path[256] = {0}, *body = NULL;
    int blen = 0;
    parse_http(buf, method, path, &body, &blen);

    if (strncmp(path, "/stream", 7) == 0) {
        const char* hdr = "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=" BOUNDARY "\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n";
        send(client, hdr, strlen(hdr), MSG_NOSIGNAL);
        while (v->running && !v->stop_requested) {
            pthread_mutex_lock(&v->frame_lock);
            if (v->frame_size > 0 && v->frame_size <= v->frame_capacity)
                send_frame(client, v->frame_buffer, v->frame_size);
            pthread_mutex_unlock(&v->frame_lock);
            fd_set fds; struct timeval tv = {0, 0};
            FD_ZERO(&fds); FD_SET(client, &fds);
            if (select(client+1, &fds, NULL, NULL, &tv) > 0) {
                char tmp; if (recv(client, &tmp, 1, MSG_PEEK) <= 0) break;
            }
            usleep(50000);
        }
    } else if (strncmp(path, "/mouse", 6) == 0 && body) {
        int x = json_int(body, "\"x\"", 0), y = json_int(body, "\"y\"", 0);
        int btn = json_int(body, "\"button\"", 0), wh = json_int(body, "\"delta\"", 0);
        int act = strstr(body, "\"up\"") ? 1 : 0;
        inject_mouse(x, y, btn, act, wh, v->display, v->width, v->height);
        send_http(client, "200 OK", "application/json", (const unsigned char*)"{\"ok\":true}", 9);
    } else if (strncmp(path, "/keyboard", 9) == 0 && body) {
        int vk = json_int(body, "\"keyCode\"", 0);
        int act = strstr(body, "\"up\"") ? 1 : 0;
        inject_keyboard(vk, act, v->display);
        send_http(client, "200 OK", "application/json", (const unsigned char*)"{\"ok\":true}", 9);
    } else if (strncmp(path, "/terminate.js", 13) == 0) {
        send_http(client, "200 OK", "application/json", (const unsigned char*)"{\"ok\":true}", 9);
        v->stop_requested = 1;
    } else {
        char page[512];
        snprintf(page, sizeof(page), "<!DOCTYPE html><html><body><h1>VNC Stream (Linux)</h1><p>Port %d</p><p><a href='/stream'>View</a></p></body></html>", v->port);
        send_http(client, "200 OK", "text/html", (const unsigned char*)page, strlen(page));
    }
    close(client);
}

/* ---- Main ---- */
int main(int argc, char* argv[])
{
    int port = DEFAULT_PORT, quality = DEFAULT_QUALITY;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i+1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--quality") == 0 && i+1 < argc) quality = atoi(argv[++i]);
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("VNC Stream Agent (Linux)\nUsage: %s [--port PORT] [--quality Q]\n", argv[0]);
            return 0;
        }
    }

    signal(SIGPIPE, SIG_IGN);
    XInitThreads();

    VncStream v;
    memset(&v, 0, sizeof(v));
    v.port = port; v.jpeg_quality = quality; v.running = 1;
    v.frame_capacity = MAX_JPEG_SIZE;
    v.frame_buffer = (unsigned char*)malloc(v.frame_capacity);
    pthread_mutex_init(&v.frame_lock, NULL);

    v.display = XOpenDisplay(NULL);
    if (!v.display) { fprintf(stderr, "[VNC] Cannot open X display\n"); return 1; }
    int scr = DefaultScreen(v.display);
    v.root = RootWindow(v.display, scr);
    v.width = DisplayWidth(v.display, scr);
    v.height = DisplayHeight(v.display, scr);

    int major, minor, pixmaps;
    if (!XShmQueryVersion(v.display, &major, &minor, &pixmaps)) {
        fprintf(stderr, "[VNC] XShm not available\n"); return 1;
    }

    v.ximage = XShmCreateImage(v.display, DefaultVisual(v.display, scr),
                                DefaultDepth(v.display, scr), ZPixmap, NULL,
                                &v.shm_info, v.width, v.height);
    if (!v.ximage) { fprintf(stderr, "[VNC] XShmCreateImage failed\n"); return 1; }

    v.shm_info.shmid = shmget(IPC_PRIVATE, v.ximage->bytes_per_line * v.ximage->height, IPC_CREAT | 0777);
    if (v.shm_info.shmid < 0) { fprintf(stderr, "[VNC] shmget failed\n"); return 1; }
    v.shm_info.shmaddr = v.ximage->data = (char*)shmat(v.shm_info.shmid, 0, 0);
    v.shm_info.readOnly = False;
    if (!XShmAttach(v.display, &v.shm_info)) { fprintf(stderr, "[VNC] XShmAttach failed\n"); return 1; }

    printf("[VNC] Linux stream: %dx%d, port %d, quality %d\n", v.width, v.height, v.port, v.jpeg_quality);

    pthread_t tid;
    pthread_create(&tid, NULL, capture_thread, &v);

    int lsock = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(v.port);

    if (bind(lsock, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
        listen(lsock, SOMAXCONN) < 0) { fprintf(stderr, "[VNC] bind/listen failed\n"); return 1; }

    printf("[VNC] Listening on 127.0.0.1:%d\n", v.port);

    while (v.running && !v.stop_requested) {
        struct sockaddr_in ca; socklen_t al = sizeof(ca);
        int client = accept(lsock, (struct sockaddr*)&ca, &al);
        if (client >= 0) handle_client(client, &v);
    }

    v.running = 0; pthread_join(tid, NULL);
    close(lsock);
    XShmDetach(v.display, &v.shm_info);
    XDestroyImage(v.ximage);
    shmdt(v.shm_info.shmaddr);
    shmctl(v.shm_info.shmid, IPC_RMID, 0);
    XCloseDisplay(v.display);
    free(v.frame_buffer);
    pthread_mutex_destroy(&v.frame_lock);
    return 0;
}
