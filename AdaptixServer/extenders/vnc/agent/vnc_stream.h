#ifndef VNC_STREAM_H
#define VNC_STREAM_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "ole32.lib")

#define DEFAULT_PORT     40484
#ifdef DEFAULT_QUALITY
#undef DEFAULT_QUALITY
#endif
#define VNC_DEFAULT_QUALITY  40
#define MAX_JPEG_SIZE    (10 * 1024 * 1024)
#define HTTP_BUF_SIZE    65536
#define MAX_SCREENS      16
#define BOUNDARY         "frame"

typedef struct {
    int   index;
    char  name[128];
    int   width;
    int   height;
    int   left;
    int   top;
} ScreenInfo;

typedef struct {
    ID3D11Device*           d3d_device;
    ID3D11DeviceContext*    d3d_context;
    IDXGIOutputDuplication* duplication;
    IDXGIOutput*            output;

    IWICImagingFactory*     wic_factory;
    IWICBitmap*             wic_bitmap;
    IWICBitmapEncoder*      jpeg_encoder;
    IWICStream*             jpeg_stream;

    ScreenInfo              screens[MAX_SCREENS];
    int                     screen_count;
    int                     current_screen;
    int                     use_gdi;
    int                     capture_left;
    int                     capture_top;
    int                     capture_width;
    int                     capture_height;

    unsigned char*          frame_buffer;
    int                     frame_size;
    int                     frame_capacity;
    SRWLOCK                 frame_lock;

    int                     jpeg_quality;
    volatile int            running;
    volatile int            stop_requested;
    volatile int            screen_switch_pending;
    volatile int            new_screen_index;

    SOCKET                  listen_socket;
    unsigned short          port;
} VncStream;

VncStream* vnc_init(int port);
void vnc_free(VncStream* v);
int vnc_start_capture(VncStream* v);
void vnc_stop_capture(VncStream* v);
int vnc_encode_frame(VncStream* v, unsigned char** out_data, int* out_size);
void vnc_inject_mouse(int x, int y, int button, int action, int wheel_delta);
void vnc_inject_keyboard(int vk_code, int action, int flags);
int vnc_run_server(VncStream* v);

#endif
