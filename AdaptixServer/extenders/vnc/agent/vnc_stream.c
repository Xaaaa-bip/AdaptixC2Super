/*
 * VNC Stream Agent - Windows Desktop Streaming with Input Injection
 * Based on SpyIt architecture: DXGI + WIC JPEG + HTTP MJPEG
 * Adds: keyboard/mouse input injection for full VNC-like remote control
 *
 * Build: cl /O2 /MT /EHsc vnc_stream.c /link d3d11.lib dxgi.lib windowscodecs.lib ws2_32.lib ole32.lib user32.lib
 * Usage: vnc_stream.exe [--port PORT] [--quality Q] [--no-detach]
 */

#include "vnc_stream.h"

/* Resolve NTSTATUS conflicts */
#undef STATUS_BUFFER_TOO_SMALL
#undef STATUS_SUCCESS
#undef STATUS_UNSUCCESSFUL
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#define STATUS_SUCCESS          ((NTSTATUS)0x00000000L)
#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL     ((NTSTATUS)0xC0000001L)
#endif

/* PrintWindow flag for rendering hardware-accelerated content on Win 8.1+ */
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

/* ---- Forward declarations ---- */
static DWORD WINAPI capture_thread(LPVOID param);
static DWORD WINAPI client_thread(LPVOID param);
static void  handle_client(SOCKET client, VncStream* v);
static int   send_http_response(SOCKET s, const char* status, const char* content_type, const unsigned char* body, int body_len);
static void  send_mjpeg_frame(SOCKET s, const unsigned char* jpeg_data, int jpeg_size);
static int   enum_screens(VncStream* v);
static int   init_dxgi(VncStream* v);
static int   init_gdi_fallback(VncStream* v);
static int   init_wic(VncStream* v);
static int   encode_jpeg(VncStream* v, unsigned char** out_data, int* out_size);
static int   encode_gdi_jpeg(VncStream* v, unsigned char** out_data, int* out_size);
static int   encode_bgra_to_jpeg(VncStream* v, unsigned char* bgra, int width, int height, unsigned char** out_data, int* out_size);
static void  overlay_cursor_gdi(VncStream* v, unsigned char* bgra, int width, int height);
static int   parse_http_request(const char* data, char* method, char* path, char** body, int* body_len);
static int   parse_json_int(const char* json, const char* key, int default_val);
static void  web_send_screens_js(SOCKET s, VncStream* v);

typedef struct {
    SOCKET client;
    VncStream* v;
} ClientContext;

/* ---- VNC Init ---- */
VncStream* vnc_init(int port)
{
    VncStream* v = (VncStream*)calloc(1, sizeof(VncStream));
    if (!v) return NULL;

    v->port = port ? port : DEFAULT_PORT;
    v->jpeg_quality = VNC_DEFAULT_QUALITY;
    v->current_screen = 0;
    v->running = 1;
    v->stop_requested = 0;
    InitializeSRWLock(&v->frame_lock);
    v->frame_capacity = MAX_JPEG_SIZE;
    v->frame_buffer = (unsigned char*)malloc(v->frame_capacity);

    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    if (!init_wic(v))  { vnc_free(v); return NULL; }
    if (!init_dxgi(v)) {
        fprintf(stderr, "[VNC] DXGI unavailable, falling back to GDI capture\n");
        if (!init_gdi_fallback(v)) { vnc_free(v); return NULL; }
    }

    printf("[VNC] Initialized: %d screen(s), port %d, quality %d, capture=%s\n",
           v->screen_count, v->port, v->jpeg_quality, v->use_gdi ? "gdi" : "dxgi");
    return v;
}

void vnc_free(VncStream* v)
{
    if (!v) return;
    v->running = 0;

    if (v->duplication)  { v->duplication->lpVtbl->Release(v->duplication); }
    if (v->d3d_context)  { v->d3d_context->lpVtbl->Release(v->d3d_context); }
    if (v->d3d_device)   { v->d3d_device->lpVtbl->Release(v->d3d_device); }
    if (v->output)       { v->output->lpVtbl->Release(v->output); }
    if (v->wic_bitmap)   { v->wic_bitmap->lpVtbl->Release(v->wic_bitmap); }
    if (v->wic_factory)  { v->wic_factory->lpVtbl->Release(v->wic_factory); }
    if (v->jpeg_encoder) { v->jpeg_encoder->lpVtbl->Release(v->jpeg_encoder); }
    if (v->jpeg_stream)  { v->jpeg_stream->lpVtbl->Release(v->jpeg_stream); }

    free(v->frame_buffer);
    WSACleanup();
    CoUninitialize();
    free(v);
}

/* ---- DXGI Init ---- */
static int init_dxgi(VncStream* v)
{
    HRESULT hr;
    IDXGIFactory1* factory = NULL;
    IDXGIAdapter1* adapter = NULL;
    IDXGIOutput* output = NULL;

    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void**)&factory);
    if (FAILED(hr)) { fprintf(stderr, "[VNC] CreateDXGIFactory1 failed: 0x%08lx\n", hr); return 0; }

    for (UINT i = 0; factory->lpVtbl->EnumAdapters1(factory, i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
        for (UINT j = 0; adapter->lpVtbl->EnumOutputs(adapter, j, &output) != DXGI_ERROR_NOT_FOUND; j++) {
            if (output) goto adapter_found;
        }
        adapter->lpVtbl->Release(adapter);
        adapter = NULL;
    }

adapter_found:
    factory->lpVtbl->Release(factory);
    if (!adapter || !output) { fprintf(stderr, "[VNC] No active display adapter found\n"); return 0; }

    D3D_FEATURE_LEVEL fl;
    hr = D3D11CreateDevice((IDXGIAdapter*)adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0,
                           NULL, 0, D3D11_SDK_VERSION, &v->d3d_device, &fl, &v->d3d_context);
    adapter->lpVtbl->Release(adapter);
    if (FAILED(hr)) { output->lpVtbl->Release(output); return 0; }

    IDXGIOutput1* output1 = NULL;
    hr = output->lpVtbl->QueryInterface(output, &IID_IDXGIOutput1, (void**)&output1);
    output->lpVtbl->Release(output);
    if (FAILED(hr)) return 0;

    hr = output1->lpVtbl->DuplicateOutput(output1, (IUnknown*)v->d3d_device, &v->duplication);
    output1->lpVtbl->Release(output1);
    if (FAILED(hr)) { fprintf(stderr, "[VNC] DuplicateOutput failed: 0x%08lx\n", hr); return 0; }

    return enum_screens(v);
}

static int enum_screens(VncStream* v)
{
    IDXGIFactory1* factory = NULL;
    CreateDXGIFactory1(&IID_IDXGIFactory1, (void**)&factory);
    if (!factory) return 0;

    v->screen_count = 0;
    IDXGIAdapter1* adapter = NULL;
    for (UINT i = 0; factory->lpVtbl->EnumAdapters1(factory, i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
        IDXGIOutput* output = NULL;
        for (UINT j = 0; adapter->lpVtbl->EnumOutputs(adapter, j, &output) != DXGI_ERROR_NOT_FOUND; j++) {
            DXGI_OUTPUT_DESC desc;
            if (SUCCEEDED(output->lpVtbl->GetDesc(output, &desc)) && v->screen_count < MAX_SCREENS) {
                ScreenInfo* s = &v->screens[v->screen_count];
                s->index = v->screen_count;
                WideCharToMultiByte(CP_UTF8, 0, desc.DeviceName, -1, s->name, sizeof(s->name), NULL, NULL);
                s->width  = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
                s->height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
                s->left   = desc.DesktopCoordinates.left;
                s->top    = desc.DesktopCoordinates.top;
                v->screen_count++;
            }
            output->lpVtbl->Release(output);
        }
        adapter->lpVtbl->Release(adapter);
    }
    factory->lpVtbl->Release(factory);
    return v->screen_count > 0;
}

static int init_gdi_fallback(VncStream* v)
{
    int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (width <= 0 || height <= 0) {
        left = 0;
        top = 0;
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
    }
    if (width <= 0 || height <= 0) {
        fprintf(stderr, "[VNC] GDI fallback failed: no active screen\n");
        return 0;
    }

    v->use_gdi = 1;
    v->capture_left = left;
    v->capture_top = top;
    v->capture_width = width;
    v->capture_height = height;
    v->screen_count = 1;
    v->screens[0].index = 0;
    snprintf(v->screens[0].name, sizeof(v->screens[0].name), "GDI Desktop");
    v->screens[0].width = width;
    v->screens[0].height = height;
    v->screens[0].left = left;
    v->screens[0].top = top;
    return 1;
}

/* ---- WIC Init ---- */
static int init_wic(VncStream* v)
{
    HRESULT hr;
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWICImagingFactory, (void**)&v->wic_factory);
    if (FAILED(hr)) return 0;
    return 1;
}

/* ---- Capture Thread ---- */
int vnc_start_capture(VncStream* v)
{
    HANDLE h = CreateThread(NULL, 0, capture_thread, v, 0, NULL);
    if (h) { CloseHandle(h); return 1; }
    return 0;
}

static DWORD WINAPI capture_thread(LPVOID param)
{
    VncStream* v = (VncStream*)param;
    // WIC (IWICImagingFactory etc.) methods require COM to be initialized on
    // the calling thread. Without this, encode_bgra_to_jpeg fails silently.
    HRESULT hrInit = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hrInit) && hrInit != RPC_E_CHANGED_MODE) {
        fprintf(stderr, "[VNC] capture_thread: CoInitializeEx failed hr=0x%08lx\n", hrInit);
    }
    DWORD iter = 0;
    DWORD success = 0;
    DWORD failure = 0;
    DWORD lastLog = GetTickCount();
    while (v->running && !v->stop_requested) {
        unsigned char* jpeg_data = NULL;
        int jpeg_size = 0;
        int rc = encode_jpeg(v, &jpeg_data, &jpeg_size);
        iter++;
        if (rc > 0 && jpeg_data) {
            AcquireSRWLockExclusive(&v->frame_lock);
            if (jpeg_size <= v->frame_capacity) {
                memcpy(v->frame_buffer, jpeg_data, jpeg_size);
                v->frame_size = jpeg_size;
            }
            ReleaseSRWLockExclusive(&v->frame_lock);
            free(jpeg_data);
            success++;
        } else {
            failure++;
        }
        DWORD now = GetTickCount();
        if (now - lastLog >= 2000) {
            fprintf(stderr, "[VNC] capture stats: iter=%lu success=%lu failure=%lu last_frame_size=%d capture=%s\n",
                iter, success, failure, v->frame_size, v->use_gdi ? "gdi" : "dxgi");
            lastLog = now;
        }
        Sleep(100);
    }
    CoUninitialize();
    return 0;
}

void vnc_stop_capture(VncStream* v) { v->stop_requested = 1; }

/* ---- JPEG Encoding ---- */
int vnc_encode_frame(VncStream* v, unsigned char** out_data, int* out_size)
{
    return encode_jpeg(v, out_data, out_size);
}

static int encode_jpeg(VncStream* v, unsigned char** out_data, int* out_size)
{
    if (v->use_gdi) {
        return encode_gdi_jpeg(v, out_data, out_size);
    }

    HRESULT hr;
    IDXGIResource* desktop_resource = NULL;
    DXGI_OUTDUPL_FRAME_INFO frame_info;
    ID3D11Texture2D* desktop_texture = NULL;
    D3D11_TEXTURE2D_DESC tex_desc;
    ID3D11Texture2D* staging_texture = NULL;
    D3D11_MAPPED_SUBRESOURCE mapped;
    int width, height, result = 0;

    hr = v->duplication->lpVtbl->AcquireNextFrame(v->duplication, 100, &frame_info, &desktop_resource);
    if (FAILED(hr)) return (hr == DXGI_ERROR_WAIT_TIMEOUT) ? 0 : -1;

    hr = desktop_resource->lpVtbl->QueryInterface(desktop_resource, &IID_ID3D11Texture2D, (void**)&desktop_texture);
    desktop_resource->lpVtbl->Release(desktop_resource);
    if (FAILED(hr)) { v->duplication->lpVtbl->ReleaseFrame(v->duplication); return -1; }

    desktop_texture->lpVtbl->GetDesc(desktop_texture, &tex_desc);
    width = (int)tex_desc.Width;
    height = (int)tex_desc.Height;

    D3D11_TEXTURE2D_DESC sd = tex_desc;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;

    hr = v->d3d_device->lpVtbl->CreateTexture2D(v->d3d_device, &sd, NULL, &staging_texture);
    if (FAILED(hr)) { desktop_texture->lpVtbl->Release(desktop_texture); v->duplication->lpVtbl->ReleaseFrame(v->duplication); return -1; }

    v->d3d_context->lpVtbl->CopyResource(v->d3d_context, (ID3D11Resource*)staging_texture, (ID3D11Resource*)desktop_texture);
    desktop_texture->lpVtbl->Release(desktop_texture);

    hr = v->d3d_context->lpVtbl->Map(v->d3d_context, (ID3D11Resource*)staging_texture, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) { staging_texture->lpVtbl->Release(staging_texture); v->duplication->lpVtbl->ReleaseFrame(v->duplication); return -1; }

    unsigned char* bgra = (unsigned char*)malloc(width * height * 4);
    if (!bgra) { v->d3d_context->lpVtbl->Unmap(v->d3d_context, (ID3D11Resource*)staging_texture, 0); staging_texture->lpVtbl->Release(staging_texture); v->duplication->lpVtbl->ReleaseFrame(v->duplication); return -1; }

    for (int y = 0; y < height; y++)
        memcpy(bgra + y * width * 4, (unsigned char*)mapped.pData + y * mapped.RowPitch, width * 4);

    overlay_cursor_gdi(v, bgra, width, height);

    result = encode_bgra_to_jpeg(v, bgra, width, height, out_data, out_size);
    free(bgra);

    v->d3d_context->lpVtbl->Unmap(v->d3d_context, (ID3D11Resource*)staging_texture, 0);
    staging_texture->lpVtbl->Release(staging_texture);
    v->duplication->lpVtbl->ReleaseFrame(v->duplication);
    return result;
}

static int encode_gdi_jpeg(VncStream* v, unsigned char** out_data, int* out_size)
{
    static int logOnce = 0;
    int width = v->capture_width;
    int height = v->capture_height;
    if (width <= 0 || height <= 0) {
        if (!logOnce) { fprintf(stderr, "[VNC] encode_gdi_jpeg: invalid dims w=%d h=%d\n", width, height); logOnce = 1; }
        return 0;
    }

    HDC screen = GetDC(NULL);
    if (!screen) {
        if (!logOnce) { fprintf(stderr, "[VNC] encode_gdi_jpeg: GetDC(NULL) returned NULL, err=%lu\n", GetLastError()); logOnce = 1; }
        return 0;
    }
    HDC memdc = CreateCompatibleDC(screen);
    if (!memdc) {
        if (!logOnce) { fprintf(stderr, "[VNC] encode_gdi_jpeg: CreateCompatibleDC failed, err=%lu\n", GetLastError()); logOnce = 1; }
        ReleaseDC(NULL, screen);
        return 0;
    }

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    unsigned char* bgra = NULL;
    HBITMAP hbmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, (void**)&bgra, NULL, 0);
    if (!hbmp || !bgra) {
        if (!logOnce) { fprintf(stderr, "[VNC] encode_gdi_jpeg: CreateDIBSection failed, err=%lu\n", GetLastError()); logOnce = 1; }
        if (hbmp) DeleteObject(hbmp);
        DeleteDC(memdc);
        ReleaseDC(NULL, screen);
        return 0;
    }

    HGDIOBJ old = SelectObject(memdc, hbmp);

    // Try BitBlt first (classic approach, works on real hardware)
    BOOL ok = BitBlt(memdc, 0, 0, width, height, screen, v->capture_left, v->capture_top, SRCCOPY | CAPTUREBLT);
    DWORD bitbltErr = GetLastError();

    // If BitBlt fails (common on headless cloud VMs without a real framebuffer),
    // fall back to PrintWindow on the desktop window which uses WM_PRINT rendering.
    if (!ok) {
        HWND desktop = GetDesktopWindow();
        if (desktop) {
            // PrintWindow requires PW_RENDERFULLCONTENT on Win8.1+ for hardware-accelerated windows
            BOOL pw = PrintWindow(desktop, memdc, PW_RENDERFULLCONTENT);
            if (!pw) {
                // Try without the flag (older Windows)
                pw = PrintWindow(desktop, memdc, 0);
            }
            if (pw) {
                ok = TRUE;
                if (!logOnce) {
                    fprintf(stderr, "[VNC] encode_gdi_jpeg: BitBlt failed (err=%lu), PrintWindow(desktop) succeeded\n", bitbltErr);
                    logOnce = 1;
                }
            } else if (!logOnce) {
                fprintf(stderr, "[VNC] encode_gdi_jpeg: BitBlt failed (err=%lu) AND PrintWindow failed (err=%lu)\n",
                    bitbltErr, GetLastError());
                logOnce = 1;
            }
        }
    } else if (!logOnce) {
        fprintf(stderr, "[VNC] encode_gdi_jpeg: BitBlt ok (first call)\n");
        logOnce = 1;
    }

    SelectObject(memdc, old);

    int result = 0;
    if (ok) {
        overlay_cursor_gdi(v, bgra, width, height);
        result = encode_bgra_to_jpeg(v, bgra, width, height, out_data, out_size);
    }

    DeleteObject(hbmp);
    DeleteDC(memdc);
    ReleaseDC(NULL, screen);
    return result;
}

static int encode_bgra_to_jpeg(VncStream* v, unsigned char* bgra, int width, int height, unsigned char** out_data, int* out_size)
{
    static int logOnce = 0;
    HRESULT hr;
    int result = 0;
    IWICBitmap* wic_bmp = NULL;
    hr = v->wic_factory->lpVtbl->CreateBitmapFromMemory(v->wic_factory, width, height,
        &GUID_WICPixelFormat32bppBGRA, width * 4, width * height * 4, bgra, &wic_bmp);
    if (FAILED(hr) || !wic_bmp) {
        if (!logOnce) { fprintf(stderr, "[VNC] encode_bgra_to_jpeg: CreateBitmapFromMemory failed hr=0x%08lx w=%d h=%d\n", hr, width, height); logOnce = 1; }
        return 0;
    }

    IWICFormatConverter* conv = NULL;
    hr = v->wic_factory->lpVtbl->CreateFormatConverter(v->wic_factory, &conv);
    if (FAILED(hr) || !conv) {
        if (!logOnce) { fprintf(stderr, "[VNC] encode_bgra_to_jpeg: CreateFormatConverter failed hr=0x%08lx\n", hr); logOnce = 1; }
        wic_bmp->lpVtbl->Release(wic_bmp);
        return 0;
    }
    hr = conv->lpVtbl->Initialize(conv, (IWICBitmapSource*)wic_bmp,
        &GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        if (!logOnce) { fprintf(stderr, "[VNC] encode_bgra_to_jpeg: FormatConverter Initialize failed hr=0x%08lx\n", hr); logOnce = 1; }
        conv->lpVtbl->Release(conv);
        wic_bmp->lpVtbl->Release(wic_bmp);
        return 0;
    }

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, MAX_JPEG_SIZE);
    if (!hMem) {
        if (!logOnce) { fprintf(stderr, "[VNC] encode_bgra_to_jpeg: GlobalAlloc failed err=%lu size=%d\n", GetLastError(), MAX_JPEG_SIZE); logOnce = 1; }
        conv->lpVtbl->Release(conv);
        wic_bmp->lpVtbl->Release(wic_bmp);
        return 0;
    }

    IStream* stream = NULL;
    hr = CreateStreamOnHGlobal(hMem, FALSE, &stream);
    if (FAILED(hr) || !stream) {
        if (!logOnce) { fprintf(stderr, "[VNC] encode_bgra_to_jpeg: CreateStreamOnHGlobal failed hr=0x%08lx\n", hr); logOnce = 1; }
        GlobalFree(hMem);
        conv->lpVtbl->Release(conv);
        wic_bmp->lpVtbl->Release(wic_bmp);
        return 0;
    }

    IWICBitmapEncoder* enc = NULL;
    hr = CoCreateInstance(&CLSID_WICJpegEncoder, NULL, CLSCTX_INPROC_SERVER,
                     &IID_IWICBitmapEncoder, (void**)&enc);
    if (FAILED(hr) || !enc) {
        if (!logOnce) { fprintf(stderr, "[VNC] encode_bgra_to_jpeg: CoCreateInstance(JpegEncoder) failed hr=0x%08lx\n", hr); logOnce = 1; }
        stream->lpVtbl->Release(stream);
        GlobalFree(hMem);
        conv->lpVtbl->Release(conv);
        wic_bmp->lpVtbl->Release(wic_bmp);
        return 0;
    }

    hr = enc->lpVtbl->Initialize(enc, stream, WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        if (!logOnce) { fprintf(stderr, "[VNC] encode_bgra_to_jpeg: Encoder Initialize failed hr=0x%08lx\n", hr); logOnce = 1; }
        enc->lpVtbl->Release(enc);
        stream->lpVtbl->Release(stream);
        GlobalFree(hMem);
        conv->lpVtbl->Release(conv);
        wic_bmp->lpVtbl->Release(wic_bmp);
        return 0;
    }

    IWICBitmapFrameEncode* fe = NULL;
    IPropertyBag2* pb = NULL;
    hr = enc->lpVtbl->CreateNewFrame(enc, &fe, &pb);
    if (FAILED(hr) || !fe) {
        if (!logOnce) { fprintf(stderr, "[VNC] encode_bgra_to_jpeg: CreateNewFrame failed hr=0x%08lx\n", hr); logOnce = 1; }
        if (pb) pb->lpVtbl->Release(pb);
        enc->lpVtbl->Release(enc);
        stream->lpVtbl->Release(stream);
        GlobalFree(hMem);
        conv->lpVtbl->Release(conv);
        wic_bmp->lpVtbl->Release(wic_bmp);
        return 0;
    }

    PROPBAG2 opt = {0};
    VARIANT var;
    opt.pstrName = L"ImageQuality";
    VariantInit(&var);
    var.vt = VT_R4;
    var.fltVal = v->jpeg_quality / 100.0f;
    pb->lpVtbl->Write(pb, 1, &opt, &var);
    VariantClear(&var);

    hr = fe->lpVtbl->Initialize(fe, pb);
    if (FAILED(hr)) {
        if (!logOnce) { fprintf(stderr, "[VNC] encode_bgra_to_jpeg: Frame Initialize failed hr=0x%08lx\n", hr); logOnce = 1; }
        goto cleanup;
    }
    hr = fe->lpVtbl->SetSize(fe, width, height);
    if (FAILED(hr)) {
        if (!logOnce) { fprintf(stderr, "[VNC] encode_bgra_to_jpeg: SetSize failed hr=0x%08lx\n", hr); logOnce = 1; }
        goto cleanup;
    }
    hr = fe->lpVtbl->WriteSource(fe, (IWICBitmapSource*)conv, NULL);
    if (FAILED(hr)) {
        if (!logOnce) { fprintf(stderr, "[VNC] encode_bgra_to_jpeg: WriteSource failed hr=0x%08lx\n", hr); logOnce = 1; }
        goto cleanup;
    }
    hr = fe->lpVtbl->Commit(fe);
    if (FAILED(hr)) {
        if (!logOnce) { fprintf(stderr, "[VNC] encode_bgra_to_jpeg: Frame Commit failed hr=0x%08lx\n", hr); logOnce = 1; }
        goto cleanup;
    }
    hr = enc->lpVtbl->Commit(enc);
    if (FAILED(hr)) {
        if (!logOnce) { fprintf(stderr, "[VNC] encode_bgra_to_jpeg: Encoder Commit failed hr=0x%08lx\n", hr); logOnce = 1; }
        goto cleanup;
    }

    {
        // After Commit, the stream's write position is at the end of the JPEG
        // data. STREAM_SEEK_END on an HGLOBAL-backed stream returns the full
        // HGLOBAL buffer size (pre-allocated to MAX_JPEG_SIZE above), NOT the
        // actual bytes written — so we must use SEEK_CUR to get the write
        // position which equals the real JPEG size.
        ULARGE_INTEGER pos;
        LARGE_INTEGER zero = {0};
        stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_CUR, &pos);
        int jpeg_size = (int)pos.QuadPart;

        if (jpeg_size > 0 && jpeg_size <= MAX_JPEG_SIZE) {
            unsigned char* jd = (unsigned char*)malloc(jpeg_size);
            if (jd) {
                stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_SET, NULL);
                ULONG read;
                stream->lpVtbl->Read(stream, jd, jpeg_size, &read);
                *out_data = jd;
                *out_size = read;
                result = 1;
                if (!logOnce) { fprintf(stderr, "[VNC] encode_bgra_to_jpeg: first frame ok, size=%d\n", jpeg_size); logOnce = 1; }
            }
        } else if (!logOnce) {
            fprintf(stderr, "[VNC] encode_bgra_to_jpeg: invalid jpeg size=%d\n", jpeg_size);
            logOnce = 1;
        }
    }

cleanup:
    if (fe) fe->lpVtbl->Release(fe);
    if (pb) pb->lpVtbl->Release(pb);
    enc->lpVtbl->Release(enc);
    stream->lpVtbl->Release(stream);
    GlobalFree(hMem);
    conv->lpVtbl->Release(conv);
    wic_bmp->lpVtbl->Release(wic_bmp);
    return result;
}

/* ---- Cursor Overlay ---- */
static void overlay_cursor_gdi(VncStream* v, unsigned char* bgra, int width, int height)
{
    CURSORINFO ci = { sizeof(ci) };
    if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING)) return;

    ICONINFO ii;
    if (!GetIconInfo(ci.hCursor, &ii)) return;

    HDC hdc = CreateCompatibleDC(NULL);
    if (!hdc) { if (ii.hbmColor) DeleteObject(ii.hbmColor); if (ii.hbmMask) DeleteObject(ii.hbmMask); return; }

    BITMAP bm;
    GetObjectA(ii.hbmColor ? ii.hbmColor : ii.hbmMask, sizeof(bm), &bm);

    int cw = bm.bmWidth;
    int ch = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
    int cx = ci.ptScreenPos.x - v->capture_left - (int)ii.xHotspot;
    int cy = ci.ptScreenPos.y - v->capture_top - (int)ii.yHotspot;

    unsigned char* cb = (unsigned char*)calloc(cw * ch, 4);
    if (!cb) { DeleteDC(hdc); if (ii.hbmColor) DeleteObject(ii.hbmColor); if (ii.hbmMask) DeleteObject(ii.hbmMask); return; }

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = cw;
    bmi.bmiHeader.biHeight = -ch;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    GetDIBits(hdc, ii.hbmColor ? ii.hbmColor : ii.hbmMask, 0, ch, cb, &bmi, DIB_RGB_COLORS);

    for (int dy = 0; dy < ch; dy++) {
        int fy = cy + dy;
        if (fy < 0 || fy >= height) continue;
        for (int dx = 0; dx < cw; dx++) {
            int fx = cx + dx;
            if (fx < 0 || fx >= width) continue;
            int ci = (dy * cw + dx) * 4;
            int fi = (fy * width + fx) * 4;
            float a = cb[ci + 3] / 255.0f;
            bgra[fi+0] = (unsigned char)(cb[ci+0] * a + bgra[fi+0] * (1-a));
            bgra[fi+1] = (unsigned char)(cb[ci+1] * a + bgra[fi+1] * (1-a));
            bgra[fi+2] = (unsigned char)(cb[ci+2] * a + bgra[fi+2] * (1-a));
        }
    }

    free(cb);
    DeleteDC(hdc);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
}

/* ---- Input Injection ---- */
void vnc_inject_mouse(int x, int y, int button, int action, int wheel_delta)
{
    INPUT inputs[4];
    int count = 0;
    memset(inputs, 0, sizeof(inputs));

    inputs[count].type = INPUT_MOUSE;
    inputs[count].mi.dx = x;
    inputs[count].mi.dy = y;
    inputs[count].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    count++;

    if (wheel_delta != 0) {
        inputs[count].type = INPUT_MOUSE;
        inputs[count].mi.dwFlags = MOUSEEVENTF_WHEEL;
        inputs[count].mi.mouseData = wheel_delta > 0 ? WHEEL_DELTA : (DWORD)(-WHEEL_DELTA);
        count++;
    }

    if (action == 0) {
        DWORD f = 0;
        if (button == 0) f = MOUSEEVENTF_LEFTDOWN;
        else if (button == 1) f = MOUSEEVENTF_RIGHTDOWN;
        else if (button == 2) f = MOUSEEVENTF_MIDDLEDOWN;
        if (f) { inputs[count].type = INPUT_MOUSE; inputs[count].mi.dwFlags = f; count++; }
    } else if (action == 1) {
        DWORD f = 0;
        if (button == 0) f = MOUSEEVENTF_LEFTUP;
        else if (button == 1) f = MOUSEEVENTF_RIGHTUP;
        else if (button == 2) f = MOUSEEVENTF_MIDDLEUP;
        if (f) { inputs[count].type = INPUT_MOUSE; inputs[count].mi.dwFlags = f; count++; }
    }

    if (count > 0) SendInput(count, inputs, sizeof(INPUT));
}

void vnc_inject_keyboard(int vk_code, int action, int flags)
{
    INPUT input;
    memset(&input, 0, sizeof(input));
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk_code;
    input.ki.dwFlags = (action == 1) ? KEYEVENTF_KEYUP : 0;
    if (flags & 1) input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &input, sizeof(INPUT));
}

/* ---- HTTP Server ---- */
int vnc_run_server(VncStream* v)
{
    struct sockaddr_in addr;
    int reuse = 1;

    v->listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (v->listen_socket == INVALID_SOCKET) return 0;

    setsockopt(v->listen_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(v->port);

    if (bind(v->listen_socket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR ||
        listen(v->listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(v->listen_socket);
        return 0;
    }

    printf("[VNC] HTTP server listening on 127.0.0.1:%d\n", v->port);

    while (v->running && !v->stop_requested) {
        struct sockaddr_in ca;
        int al = sizeof(ca);
        SOCKET client = accept(v->listen_socket, (struct sockaddr*)&ca, &al);
        if (client != INVALID_SOCKET) {
            ClientContext* ctx = (ClientContext*)malloc(sizeof(ClientContext));
            if (!ctx) {
                closesocket(client);
                continue;
            }
            ctx->client = client;
            ctx->v = v;
            HANDLE h = CreateThread(NULL, 0, client_thread, ctx, 0, NULL);
            if (h) CloseHandle(h);
            else { closesocket(client); free(ctx); }
        }
    }
    closesocket(v->listen_socket);
    return 1;
}

static DWORD WINAPI client_thread(LPVOID param)
{
    ClientContext* ctx = (ClientContext*)param;
    SOCKET client = ctx->client;
    VncStream* v = ctx->v;
    free(ctx);
    handle_client(client, v);
    return 0;
}

static void handle_client(SOCKET client, VncStream* v)
{
    char buf[HTTP_BUF_SIZE];
    int timeout = 5000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    int rlen = recv(client, buf, sizeof(buf) - 1, 0);
    if (rlen <= 0) { closesocket(client); return; }
    buf[rlen] = '\0';

    char method[16] = {0}, path[256] = {0}, *body = NULL;
    int body_len = 0;
    parse_http_request(buf, method, path, &body, &body_len);

    if (strncmp(path, "/stream", 7) == 0) {
        const char* hdr = "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=" BOUNDARY "\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n";
        send(client, hdr, (int)strlen(hdr), 0);

        while (v->running && !v->stop_requested) {
            AcquireSRWLockShared(&v->frame_lock);
            if (v->frame_size > 0 && v->frame_size <= v->frame_capacity)
                send_mjpeg_frame(client, v->frame_buffer, v->frame_size);
            ReleaseSRWLockShared(&v->frame_lock);

            fd_set fds; struct timeval tv = {0, 0};
            FD_ZERO(&fds); FD_SET(client, &fds);
            if (select(0, &fds, NULL, NULL, &tv) > 0) {
                char tmp; if (recv(client, &tmp, 1, MSG_PEEK) <= 0) break;
            }
            Sleep(50);
        }

    } else if (strncmp(path, "/mouse", 6) == 0 && body) {
        int x = parse_json_int(body, "\"x\"", 0);
        int y = parse_json_int(body, "\"y\"", 0);
        int btn = parse_json_int(body, "\"button\"", 0);
        int wh = parse_json_int(body, "\"delta\"", 0);
        int act = strstr(body, "\"up\"") ? 1 : strstr(body, "\"wheel\"") ? 3 : 0;

        if (strstr(body, "\"clientWidth\"")) {
            int nw = parse_json_int(body, "\"width\"", 1920);
            int nh = parse_json_int(body, "\"height\"", 1080);
            int cw = parse_json_int(body, "\"clientWidth\"", nw);
            int ch = parse_json_int(body, "\"clientHeight\"", nh);
            if (cw > 0 && ch > 0) {
                x = (x * nw / cw) * 65535 / nw;
                y = (y * nh / ch) * 65535 / nh;
            }
        }
        vnc_inject_mouse(x, y, btn, act, wh);
        send_http_response(client, "200 OK", "application/json", (const unsigned char*)"{\"ok\":true}", 9);

    } else if (strncmp(path, "/keyboard", 9) == 0 && body) {
        int vk = parse_json_int(body, "\"keyCode\"", 0);
        int act = strstr(body, "\"up\"") ? 1 : 0;
        int fl = 0;
        if (strstr(body, "\"ctrl\":true") || strstr(body, "\"ctrl\": true")) fl |= 1;
        vnc_inject_keyboard(vk, act, fl);
        send_http_response(client, "200 OK", "application/json", (const unsigned char*)"{\"ok\":true}", 9);

    } else if (strncmp(path, "/screens.js", 11) == 0) {
        web_send_screens_js(client, v);

    } else if (strncmp(path, "/switch.js", 10) == 0) {
        const char* q = strstr(path, "screen=");
        if (q) {
            int sc = atoi(q + 7);
            if (sc >= 0 && sc < v->screen_count) {
                v->new_screen_index = sc;
                v->screen_switch_pending = 1;
            }
        }
        send_http_response(client, "200 OK", "application/json", (const unsigned char*)"{\"ok\":true}", 9);

    } else if (strncmp(path, "/terminate.js", 13) == 0) {
        send_http_response(client, "200 OK", "application/json", (const unsigned char*)"{\"ok\":true}", 9);
        v->stop_requested = 1;

    } else {
        char page[1024];
        snprintf(page, sizeof(page),
            "<!DOCTYPE html><html><head><title>VNC Status</title><style>"
            "body{background:#1a1a2e;color:#e0e0e0;font-family:sans-serif;text-align:center;padding-top:50px}"
            "</style></head><body>"
            "<h1>VNC Stream Active</h1><p>Server running on port %d</p>"
            "<p>Endpoints: <code>/stream</code> (MJPEG), <code>/mouse</code>, <code>/keyboard</code>, "
            "<code>/screens.js</code>, <code>/terminate.js</code></p>"
            "<p><a href=\"/stream\" style=\"color:#e94560\">View Stream Directly</a></p>"
            "</body></html>", v->port);
        send_http_response(client, "200 OK", "text/html", (const unsigned char*)page, (int)strlen(page));
    }

    closesocket(client);
}

/* ---- HTTP Utilities ---- */
static int send_http_response(SOCKET s, const char* status, const char* ct,
                              const unsigned char* body, int blen)
{
    char hdr[1024];
    int hl = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n\r\n",
        status, ct, blen);
    send(s, hdr, hl, 0);
    if (body && blen > 0) send(s, (const char*)body, blen, 0);
    return 0;
}

static void send_mjpeg_frame(SOCKET s, const unsigned char* jpeg_data, int jpeg_size)
{
    char ph[256];
    int pl = snprintf(ph, sizeof(ph),
        "--" BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n", jpeg_size);
    send(s, ph, pl, 0);
    send(s, (const char*)jpeg_data, jpeg_size, 0);
    send(s, "\r\n", 2, 0);
}

static int parse_http_request(const char* data, char* method, char* path,
                              char** body, int* body_len)
{
    const char* eol = strstr(data, "\r\n");
    if (!eol) return 0;

    const char* s1 = strchr(data, ' ');
    const char* s2 = s1 ? strchr(s1 + 1, ' ') : NULL;
    if (!s1 || !s2) return 0;

    int ml = (int)(s1 - data); if (ml >= 15) ml = 14;
    memcpy(method, data, ml);
    int pl = (int)(s2 - s1 - 1); if (pl >= 254) pl = 253;
    memcpy(path, s1 + 1, pl);

    const char* bs = strstr(data, "\r\n\r\n");
    if (bs) { *body = (char*)bs + 4; *body_len = (int)(data + strlen(data) - *body); if (*body_len < 0) *body_len = 0; }
    return 1;
}

static int parse_json_int(const char* json, const char* key, int def)
{
    const char* p = strstr(json, key);
    if (!p) return def;
    p += strlen(key);
    while (*p == ':' || *p == ' ' || *p == '\t') p++;
    return atoi(p);
}

static void web_send_screens_js(SOCKET s, VncStream* v)
{
    char buf[4096];
    int off = snprintf(buf, sizeof(buf), "window.__screens({count:%d,screens:[", v->screen_count);
    for (int i = 0; i < v->screen_count; i++) {
        ScreenInfo* si = &v->screens[i];
        off += snprintf(buf + off, sizeof(buf) - off,
            "%s{\"index\":%d,\"name\":\"%s\",\"width\":%d,\"height\":%d,\"left\":%d,\"top\":%d}",
            i > 0 ? "," : "", si->index, si->name, si->width, si->height, si->left, si->top);
    }
    off += snprintf(buf + off, sizeof(buf) - off, "]});\r\n");
    send_http_response(s, "200 OK", "application/javascript", (const unsigned char*)buf, off);
}

/* ---- Main ---- */
int main(int argc, char* argv[])
{
    int port = DEFAULT_PORT;
    int quality = VNC_DEFAULT_QUALITY;
    int detach = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--quality") == 0 && i + 1 < argc) quality = atoi(argv[++i]);
        else if (strcmp(argv[i], "--no-detach") == 0) detach = 0;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("VNC Stream Agent\nUsage: %s [--port PORT] [--quality Q] [--no-detach]\n", argv[0]);
            return 0;
        }
    }

    if (detach) FreeConsole();

    VncStream* v = vnc_init(port);
    if (!v) { fprintf(stderr, "Failed to initialize VNC stream\n"); return 1; }

    v->jpeg_quality = quality;

    if (!vnc_start_capture(v)) { fprintf(stderr, "Failed to start capture thread\n"); vnc_free(v); return 1; }

    vnc_run_server(v);
    vnc_free(v);
    return 0;
}
