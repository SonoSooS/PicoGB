#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <assert.h>

#include "mi.h"
#include "microcode.h"
#include "ppu.h"
#include "fabric.h"

#include "lru.h"

//#define DEBUGFRAME

#ifdef DEBUGFRAME
#define FRAME_WIDTH 256
#define FRAME_HEIGHT 154
#define FRAME_OFFX 0
#define FRAME_OFFY 10
#else
#define FRAME_WIDTH 160
#define FRAME_HEIGHT 144
#define FRAME_OFFX 8
#define FRAME_OFFY 0
#endif


#if CONFIG_DBG
var _IS_DBG;
#endif

//const char* rompath = "../testrom.gb";
//const char* rompath = "../testrom_cgb.gbc";
//const char* rompath = "C:\\Data\\ROM\\GB\\Castlevania - The Adventure (Europe).gb";
//const char* rompath = "C:\\Downloads\\dmg-acid2.gb";
//const char* rompath = "C:\\Downloads\\DebugYellow.gbc";
//const char* rompath = "C:\\Downloads\\game.gb";
const char* rompath = "C:\\Downloads\\cpu_instrs.gb";
//const char* rompath = "C:\\Downloads\\03-op sp,hl.gb";
//const char* rompath = "C:\\Data\\ROM\\GB\\Castlevania - The Adventure (Europe).gb";
//const char* rompath = "C:\\Data\\ROM\\GB\\Felix the Cat (USA, Europe).gb";

#if CONFIG_BOOTMEME
const word CPU_START = 0;
#else
const word CPU_START = 0x100;
#endif

const word IS_CGB = CONFIG_IS_CGB;


static HMODULE ntdll = 0;

static int(WINAPI*NtDelayExecution)(int doalert, int64_t* timeptr) = 0;
static int(WINAPI*NtQuerySystemTime)(int64_t* timeptr) = 0;

#if CONFIG_VSYNC == 1
static HMODULE dwm;
static HRESULT(WINAPI*DwmFlush)(void);
#elif CONFIG_VSYNC == 2
#endif


#if CONFIG_VSYNC == 2
struct
{
    int64_t tickdiff;
    int32_t oldsleep;
    int32_t deltasleep;
} mmidi_timer;


static bool mmidi_sleep(int32_t winticks)
{
    if(!NtQuerySystemTime || !NtDelayExecution)
        return false;
    
    int64_t ticker;
    
    int32_t sleeptime = winticks;
    
    NtQuerySystemTime(&ticker);
    
    uint32_t tdiff = (int32_t)(ticker - mmidi_timer.tickdiff);
    mmidi_timer.tickdiff = ticker;
    
    int32_t delt = (int32_t)(tdiff - mmidi_timer.oldsleep);
    mmidi_timer.oldsleep = sleeptime;
    
    int32_t deltasleep = mmidi_timer.deltasleep + delt;
    
    if(deltasleep > 0)
        sleeptime -= deltasleep;
    
    mmidi_timer.deltasleep = deltasleep;
    
    if(sleeptime <= 0)
    {
        if(deltasleep > 100000)
        {
            deltasleep = 100000;
            mmidi_timer.deltasleep = deltasleep;
        }
        
        return false;
    }
    else
    {
        int64_t realsleep = -sleeptime;
        NtDelayExecution(0, &realsleep);
        
        return true;
    }
}

static void mmidi_reset(void)
{
    memset(&mmidi_timer, 0, sizeof(mmidi_timer));
    
    if(!NtQuerySystemTime)
        return;
    
    NtQuerySystemTime(&mmidi_timer.tickdiff);
}
#endif

static void dis_alloc(struct mi_dispatch* __restrict dis)
{
    dis->WRAM = malloc(0x1000 * 8);
    dis->VRAM = malloc(0x2000 * 2);
    dis->SRAM = malloc(0x2000 * 4);
    
    dis->BANK_ROM = 1;
    dis->BANK_WRAM = 1;
    dis->BANK_VRAM = 0;
    dis->BANK_SRAM = 0;
    
    dis->HRAM = malloc(0x100);
    dis->OAM = malloc(0x100);
}

static void regdump(const struct mb_state* __restrict mb)
{
    puts("");
    puts("Regdump:");
    printf("- BC: %04X\n", mb->reg.BC);
    printf("- DE: %04X\n", mb->reg.DE);
    printf("- HL: %04X\n", mb->reg.HL);
    printf("-  A: %02X\n", mb->reg.A);
    printf("-  F: %01Xx\n", mb->reg.F >> 4);
    printf("- SP: %04X\n", mb->SP);
    printf("- PC: %04X\n", mb->PC);
    printf("- IE: %02X (%u:%u)\n", mb->IE, mb->IME, mb->IME_ASK);
    printf("- IF: %02X\n", mb->IF & 0x1F);
    printf("-DIV: %4X\n", mb->DIV);
}


word cb_IO(void* userdata, word addr, word data, word type)
{
    if(addr == 0xFF44) return pgf_cb_IO_(userdata, addr, data, type);
    if(addr == 0xFF41) return pgf_cb_IO_(userdata, addr, data, type);
    
#if CONFIG_DBG_IO
    if(!type)
        printf("- /RD %04X -> ", addr);
    else
        printf("- /WR %04X <- %02X | ", addr, data);
#endif
    word res = pgf_cb_IO_(userdata, addr, data, type);
#if CONFIG_DBG_IO
    printf("%02X\n", res);
#endif
    return res;
}

word cb_ROM(void* userdata, word addr, word data, word type)
{
    assert(addr < 0x8000);
    assert(type == 1);
    
#if CONFIG_DBG_IO
    if(!type)
        printf("- /MR %04X -> ", addr);
    else
        printf("- /MW %04X <- %02X | ", addr, data);
#endif
    word res = pgf_cb_ROM_(userdata, addr, data, type);
#if CONFIG_DBG_IO
    printf("%02X\n", res);
#endif
    return res;
}

const DWORD windowstyle = WS_OVERLAPPED | WS_CLIPCHILDREN | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_GROUP | WS_TABSTOP;
const DWORD windowexstyle = WS_EX_WINDOWEDGE | WS_EX_APPWINDOW;


__attribute__((no_instrument_function)) static LRESULT CALLBACK WindowProc(HWND wnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg)
    {
        case WM_CREATE:
        {
            LPVOID createparams = ((LPCREATESTRUCTW)lParam)->lpCreateParams;
            if(!createparams)
            {
                SetWindowLongPtrW(wnd, 0, (LONG_PTR)0);
                
                puts("Missing createparams");
                break;
            }
            
            SetWindowLongPtrW(wnd, 0, (LONG_PTR)createparams);
            
            LRESULT cwresult = DefWindowProcW(wnd, uMsg, wParam, lParam);
            
            //fuck lose10 for breaking a such simple thing as AdjustWindowRect ª_ª
            
            RECT wndrect;
            RECT clirect;
            if(GetWindowRect(wnd, &wndrect) && GetClientRect(wnd, &clirect))
            {
                /*
                WND:  320; 180 - 1600; 900
                CLI:    0;   0 - 1264; 681
                ADJ:  304; 141 - 1296; 759
                ACT:           - 1282; 752
                TGT:           - 
                */
                
                //printf("WND: %4i;%4i - %4i;%4i\n", wndrect.left, wndrect.top, wndrect.right, wndrect.bottom);
                //printf("CLI: %4i;%4i - %4i;%4i\n", clirect.left, clirect.top, clirect.right, clirect.bottom);
                
                LONG width = wndrect.right - wndrect.left; // 1280
                LONG height = wndrect.bottom - wndrect.top; // 720
                
                RECT calc;
                calc.left = wndrect.left - ((width - clirect.right) >> 1); //kinda works
                calc.top = wndrect.top - ((height - clirect.bottom) >> 1); //close enough
                calc.right = (width * 2) - clirect.right - 1 + 1;
                calc.bottom = (height * 2) - clirect.bottom - 1 + 1;
                
                //printf("ADJ: %4i;%4i - %4i;%4i\n", calc.left, calc.top, calc.right, calc.bottom);
                
                SetWindowPos(wnd, 0, calc.left, calc.top, calc.right, calc.bottom, SWP_FRAMECHANGED);
            }
            
            return cwresult;
        }
        
        case WM_SIZE:
            InvalidateRect(wnd, NULL, FALSE);
            break;
        
        case WM_EXITSIZEMOVE:
        {
            RECT cli;
            cli.left = 0;
            cli.top = 0;
            cli.right = FRAME_WIDTH;
            cli.bottom = FRAME_HEIGHT;
            if(!GetClientRect(wnd, &cli))
                break;
            
            LONG dw1, dw2;
            
            dw1 = (cli.right - cli.left) / FRAME_WIDTH;
            dw2 = (cli.bottom - cli.top) / FRAME_HEIGHT;
            
            if(!dw1 && !dw2)
                break;
            
            RECT adj;
            adj.left = 0;
            adj.top = 0;
            adj.right = 0;
            adj.bottom = 0;
            
            if(dw1 < dw2)
                dw2 = dw1;
            else
                dw1 = dw2;
            
            if(dw1)
                adj.right = (cli.left + (dw1 * FRAME_WIDTH)) - cli.right;
            
            if(dw2)
                adj.bottom = (cli.top + (dw2 * FRAME_HEIGHT)) - cli.bottom;
            
            
            if(!GetWindowRect(wnd, &cli))
                break;
            
            cli.left += adj.left;
            cli.top += adj.top;
            cli.right += adj.right;
            cli.bottom += adj.bottom;
            
            SetWindowPos(wnd, NULL, 0, 0, cli.right - cli.left, cli.bottom - cli.top, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOZORDER);
            
            
            break;
        }
        
        case WM_KEYDOWN:
        case WM_KEYUP:
        {
            var nbit = 0;
            
            switch(wParam)
            {
                case 'X': nbit = 1 << 0; break;
                case 'C': nbit = 1 << 1; break;
                case VK_BACK: nbit = 1 << 2; break;
                case VK_RETURN: nbit = 1 << 3; break;
                
                case VK_RIGHT: nbit = 1 << 4; break;
                case VK_LEFT: nbit = 1 << 5; break;
                case VK_UP: nbit = 1 << 6; break;
                case VK_DOWN: nbit = 1 << 7; break;
            }
            
            if(nbit)
            {
                struct pgf_userdata_t* __restrict ud = (struct pgf_userdata_t* __restrict)GetWindowLongPtrW(wnd, 0);
                
                if(!(lParam & (1 << 31)))
                {
                    ud->mb->IF |= 0x10;
                    ud->JOYP_RAW |= nbit;
                }
                else
                {
                    ud->JOYP_RAW &= ~nbit;
                }
            }
            
            break;
        }
            
        
        case WM_PAINT:
        {
            PAINTSTRUCT pstr;
            HDC dc = BeginPaint(wnd, &pstr);
            
            const struct pgf_userdata_t* __restrict ud = (const struct pgf_userdata_t* __restrict)GetWindowLongPtrW(wnd, 0);
            
            DWORD buf[(sizeof(BITMAPINFOHEADER) + (4 * sizeof(RGBQUAD))) / sizeof(DWORD)];
            BITMAPINFOHEADER* bmi = (BITMAPINFOHEADER*)buf;
            #if PPU_IS_MONOCHROME
            RGBQUAD* palette = (RGBQUAD*)((&buf[0]) + (sizeof(BITMAPINFOHEADER) / sizeof(DWORD)));
            #endif
            ZeroMemory(buf, sizeof(buf));
            
            bmi->biSize = sizeof(*bmi);
            
            bmi->biPlanes = 1;
            bmi->biWidth = 256;
            bmi->biHeight = -(144 + FRAME_OFFY);
            
            #if PPU_IS_MONOCHROME
            bmi->biBitCount = 1;
            palette[0] = (RGBQUAD){0xAB, 0xB3, 0xB6, 0x00};
            palette[1] = (RGBQUAD){0x26, 0x26, 0x25, 0x00};
            
            bmi->biClrUsed = 2;
            bmi->biClrImportant = bmi->biClrUsed;
            #else
            bmi->biBitCount = 32;
            #endif
            
            bmi->biSizeImage = (bmi->biWidth * -bmi->biHeight * bmi->biBitCount) >> 3;
            bmi->biCompression = BI_RGB;
            
            RECT cli;
            cli.left = 0;
            cli.top = 0;
            cli.right = FRAME_WIDTH;
            cli.bottom = FRAME_HEIGHT;
            GetClientRect(wnd, &cli);
            
            DWORD dw1, dw2;
            
            dw1 = (cli.right - cli.left) / FRAME_WIDTH;
            dw2 = (cli.bottom - cli.top) / FRAME_HEIGHT;
            
            if(dw1 < dw2)
                dw2 = dw1;
            else
                dw1 = dw2;
            
            if(dw1)
                cli.right = cli.left + (dw1 * FRAME_WIDTH);
            
            if(dw2)
                cli.bottom = cli.top + (dw2 * FRAME_HEIGHT);
            
            StretchDIBits
            (
                dc,
                0, 0, cli.right - cli.left, cli.bottom - cli.top,
                FRAME_OFFX, 0, FRAME_WIDTH, FRAME_HEIGHT,
                ud->ppu->state.framebuffer[0],
                (BITMAPINFO*)bmi,
                DIB_RGB_COLORS,
                SRCCOPY
            );
            
            /*
            WCHAR fmtbuf[128];
            TextOutW(dc, 0, 0, fmtbuf, swprintf(fmtbuf, sizeof(fmtbuf) / sizeof(fmtbuf[0]), L"STAT=%02X LYC=%02X LY=%02X LCDC=%02X", ud->ppu->rSTAT, ud->ppu->rLYC, ud->ppu->state.scanY, ud->ppu->rLCDC));
            */
            
            /*
            HFONT hFont = (HFONT)GetStockObject(SYSTEM_FIXED_FONT);
            HFONT hOldFont = (HFONT)SelectObject(dc, hFont);
            
            char sbuf[128];
            int nch;
            
            nch = sprintf(sbuf, "ptr: %16llX / data: %02X\n", ud->ppu->OAM, ud->_debug);
            TextOutA(dc, 0, 0, sbuf, nch);
            
            var i;
            const r8* __restrict oam = &ud->ppu->OAM[0];
            for(i = 0; i != 10; i++)
            {
                nch = sprintf(sbuf, "%02X: %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X\n",
                    i << 4,
                    oam[0],
                    oam[1],
                    oam[2],
                    oam[3],
                    oam[4],
                    oam[5],
                    oam[6],
                    oam[7],
                    oam[8],
                    oam[9],
                    oam[10],
                    oam[11],
                    oam[12],
                    oam[13],
                    oam[14],
                    oam[15]
                    );
                
                TextOutA(dc, 0, (i + 1) << 4, sbuf, nch);
                
                oam += 16;
            }
            
            SelectObject(dc, hOldFont);
            */
            
            EndPaint(wnd, &pstr);
            
            break;
        }
        
        case WM_CLOSE:
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            break;
        }
    }
    
    return DefWindowProcW(wnd, uMsg, wParam, lParam);
}

static HWND wnd_create(const struct pgf_userdata_t* __restrict ud)
{
    const LPCWSTR WClassName = L"PicoGB_Main";
    
    WNDCLASSW wndclass;
    ZeroMemory(&wndclass, sizeof(wndclass));
    wndclass.style = CS_OWNDC;
    wndclass.lpfnWndProc = WindowProc;
    wndclass.hInstance = GetModuleHandleA(0);
    wndclass.hCursor = LoadCursorA(0, IDC_CROSS);
    wndclass.hbrBackground = GetStockObject(BLACK_BRUSH);
    wndclass.lpszClassName = WClassName;
    wndclass.cbWndExtra = sizeof(word*);
    
    if(!RegisterClassW(&wndclass))
        return 0;
    
    RECT rekt;
    rekt.left = 0;
    rekt.top = 0;
    rekt.right = GetSystemMetrics(SM_CXSCREEN);
    rekt.bottom = GetSystemMetrics(SM_CYSCREEN);
    
    word want_scale = 5;
    var want_width = FRAME_WIDTH * want_scale;
    var want_height = FRAME_HEIGHT * want_scale;
    
    if(rekt.right >= want_width || rekt.bottom >= want_height)
    {
        rekt.left = (rekt.right - want_width) >> 1;
        rekt.top = (rekt.bottom - want_height) >> 1;
        rekt.right = want_width;
        rekt.bottom = want_height;
    }
    
    /*
    UINT_PTR creatparam[2] =
    {
        (UINT_PTR)ud,
        (UINT_PTR)&rekt
    };*/
    
    HWND wnd = CreateWindowExW
    (
        windowexstyle,
        WClassName,
        L"PicoGB (debug)"
        ,
        windowstyle,
        rekt.left,
        rekt.top,
        rekt.right,
        rekt.bottom,
        0,
        0,
        wndclass.hInstance,
        (LPVOID)(UINT_PTR)ud
    );
    
    if(wnd)
    {
        ShowWindow(wnd, SW_SHOWNORMAL);
    }
    
    return wnd;
}

static void wnd_update_loop(HWND wnd)
{
    MSG msg;
    while(GetMessageW(&msg, 0, 0, 0))
    {
        //TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static bool wnd_update(HWND wnd)
{
    MSG msg;
    while(PeekMessageW(&msg, 0, 0, 0, PM_REMOVE))
    {
        //TranslateMessage(&msg);
        DispatchMessageW(&msg);
        
        if(msg.message == WM_QUIT)
            return false;
    }
    
    return true;
}

static void wnd_blit(HWND wnd, const struct ppu_t* __restrict pp)
{
    InvalidateRect(wnd, NULL, FALSE);
    
    WCHAR titlestr[128];
    swprintf(titlestr, sizeof(titlestr) / sizeof(titlestr[0]), L"PicoGB (line %u)", pp->state.scanY);
    
    SetWindowTextW(wnd, titlestr);
    
    UpdateWindow(wnd);
}

#if CONFIG_DOCTOR
word mbh_fr_get(struct mb_state* __restrict mb, word Fin);

static void dbg_dump(struct mb_state* __restrict mb)
{
    var PC = mb->PC;
    if(PC)
        PC -= 1;
    
    var flag = mb->reg.F;
    var fmode = mb->FMC_MODE;
    flag = mbh_fr_get(mb, flag);
    //if(flag != mb->reg.F)
    //    printf("! FLAG CHANGE %02X --> %02X with Mode%u (%02X op %02X)\n", mb->reg.F, flag, fmode, mb->FR1, mb->FR2);
    
    printf
    (
        "A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X SP:%04X PC:%04X PCMEM:%02X, POSTFLAG:%02X\n",
        mb->reg.A,
        flag,
        mb->reg.B,
        mb->reg.C,
        mb->reg.D,
        mb->reg.E,
        mb->reg.H,
        mb->reg.L,
        mb->SP,
        PC,
        mb->IR.low,
        mb->reg.F
    );
    
    mb->reg.F = flag;
}
#endif

#if CONFIG_APU_ENABLE
#define N_OUT 8

static WAVEFORMATEX wavfmt1;
static HWAVEOUT hwow = 0;
static WAVEHDR bufo[N_OUT];
static DWORD toggo = 0;
static DWORD cnto = N_OUT;
#endif


static struct pgf_userdata_t userdata;
static struct mi_dispatch dis;
static struct mb_state mb;
static struct ppu_t pp;
static struct apu_t apu;
static const r8* __restrict rommap[512];

static pixel_t* __restrict fb_lines[145];

#if CONFIG_ENABLE_LRU
static struct lru_slot lru_slots[32];
static struct lru_state lru =
{
    .slots = &lru_slots[0],
    .slots_count = sizeof(lru_slots) / sizeof(lru_slots[0])
};
static r8 lru_bufs[sizeof(lru_slots) / sizeof(lru_slots[0])][1 << MICACHE_R_BITS];
struct mi_dispatch_ROM_Bank lru_dispatch;

const r8* cb_ROM_LRU(void* userdata, word addr, word bank)
{
    printf("! LRU %02X:%04X\n", bank, addr);
    const r8* __restrict dataptr = rommap[bank];
    if(dataptr)
    {
        dataptr = &dataptr[(addr & 0x3FFF) & ~MICACHE_R_SEL];
        return dataptr;
    }
    
    puts("!!! BAD LRU");
    
    return 0;
}
#endif

//#define F(offs,fld) if(offsetof(mb_state,fld)!=offs) {printf("&" #fld " != " #offs " (%u)\n", offsetof(mb_state,fld));assert(offsetof(mb_state, fld) == offs);}
int main(int argc, char** argv)
{
    //puts("Emulator test");
    
    //F(0, reg.BC);
    //F(2, reg.DE);
    //F(4, reg.HL);
    //F(0, reg.C);
    //F(1, reg.B);
    //F(2, reg.E);
    //F(3, reg.D);
    //F(4, reg.L);
    //F(5, reg.H);
    //F(6, reg.A);
    //F(7, reg.F);
    
    memset(&dis, 0, sizeof(dis));
    memset(&mb, 0, sizeof(mb));
    memset(&pp, 0, sizeof(pp));
    memset(&apu, 0, sizeof(apu));
    
    var i,j;
    
    memset((void*)&rommap[0], 0, sizeof(rommap));
    pixel_t* __restrict framebuffer = malloc(sizeof(pixel_t) * 256 * 145);
    memset(framebuffer, 0, sizeof(pixel_t) * 256 * 145);
    for(i = 0; i != 145; ++i)
    {
        #if PPU_IS_MONOCHROME
        fb_lines[i] = &framebuffer[(256 * i) / PPU_IS_IPP];
        #else
        fb_lines[i] = &framebuffer[256 * i];
        #endif
        
        for(j = 0; j != 160; ++j)
            fb_lines[i][j] = rand();
    }
    
    ntdll = LoadLibraryA("ntdll");
    if(ntdll)
    {
        NtDelayExecution = (void*)GetProcAddress(ntdll, "NtDelayExecution");
        NtQuerySystemTime = (void*)GetProcAddress(ntdll, "NtQuerySystemTime");
        
        if(!NtDelayExecution || !NtQuerySystemTime)
        {
            puts("WARNING: Missing ntdll timing functions, no VSync");
            FreeLibrary(ntdll);
            ntdll = 0;
            
            NtDelayExecution = 0;
            NtQuerySystemTime = 0;
        }
    }
    
    if(!ntdll)
    {
        puts("Missing ntdll");
        return 1;
    }
    
    {
        FILE* fi = fopen(argc > 1 ? argv[1] : rompath, "rb");
        if(!fi)
        {
            puts("ROM open failure");
            return 1;
        }
        
        fseek(fi, 0, SEEK_END);
        ssize_t fs = ftell(fi);
        if((fs <= 0x7FFF) || (fs & 0x3FFF))
        {
            fclose(fi);
            puts("Bad ROM size");
            return 1;
        }
        fseek(fi, 0, SEEK_SET);
        
        ssize_t ff = fs;
        
        if(fs & (fs - 1))
        {
            do
                ff |= ff >> 1;
            while(ff & (ff + 1));
            
            ++ff;
        }
        
        r8* img = malloc(ff);
        if(fread(img, fs, 1, fi) != 1)
        {
            puts("Bad read");
            fclose(fi);
        }
        fclose(fi);
        
        if(fs != ff)
        {
            puts("WARNING: invalid ROM size, filling rest with $FF.");
            
            memset(img + fs, 0xFF, ff - fs);
            
            fs = ff;
        }
        
        for(i = 0; i != (fs >> 14); i++)
            rommap[i] = &img[i << 14];
        
    #if !CONFIG_ENABLE_LRU
        dis.ROM = rommap;
    #else
        dis.ROM = 0;
        dis.dispatch_ROM_Bank = cb_ROM_LRU;
        lru.slots = &lru_slots[0];
        lru.slots_count = sizeof(lru_slots) / sizeof(lru_slots[0]);
        for(i = 0; i != lru.slots_count; i++)
            lru_slots[i].data = &lru_bufs[i][0];
        lru_init(&lru);
        lru_dispatch.lru = &lru;
        lru_dispatch.userdata = 0;
        lru_dispatch.dispatch =  cb_ROM_LRU;
        dis.dispatch_ROM_Bank = pgf_cb_ROM_LRU_;
        dis.userdata_ROM_Bank = (void*)&lru_dispatch;
    #endif
        
        dis_alloc(&dis);
        
        mb.mi = &dis;
        micache_invalidate(&mb.micache);
        mi_params_from_header(mb.mi, rommap[0]);
        
        mb.IE = 0;
        mb.IF = 0;
        mb.IME = 0;
        mb.IME_ASK = 0;
        mb.PC = CPU_START;
        mb.IR.raw = 0;
        
        ppu_initialize(&pp);
        pp.is_cgb = IS_CGB;
        pp.VRAM = dis.VRAM;
        pp.OAM = dis.OAM;
        pp.state.framebuffer = fb_lines;
        
        apu_initialize(&apu);
        apu_reset(&apu);
        
        #if CONFIG_DBG
        _IS_DBG = 1;
        #endif
        
        if(CPU_START)
        {
            memset(dis.WRAM, 0, 0x2000);
            memset(dis.VRAM, 0, 0x2000);
            
            mb.reg.A = 0x01;
            mb.reg.BC = 0x0013;
            mb.reg.DE = 0x00D8;
            mb.reg.HL = 0x014D;
            mb.SP = 0xFFFE;
            mb.reg.F = 0xB0;
            
            pp.rLCDC = 0x91;
            pp.rSTAT = 0x85;
            
            pp.state.scanY = 0x90;
            
            mb.DIV = 0x26E168;
            
            apu.CTR_DIV = mb.DIV;
            apu.MASTER_CFG = 0xF1F377;
            
            apu_write(&apu, 0x26, 0x00);
            apu_write(&apu, 0x26, 0x8F);
            apu_write(&apu, 0x26, 0x00);
            
            apu_write(&apu, 0x26, 0xF0);
            apu_write(&apu, 0x25, 0xF3);
            apu_write(&apu, 0x24, 0x77);
            
            apu_write(&apu, 0x10, 0x80);
            apu_write(&apu, 0x11, 0x80);
            apu_write(&apu, 0x12, 0xF3);
            apu_write(&apu, 0x13, 0xC1);
            apu_write(&apu, 0x14, 0x87);
            
            apu_write(&apu, 0x16, 0x00);
            apu_write(&apu, 0x17, 0x00);
            apu_write(&apu, 0x18, 0x00);
            apu_write(&apu, 0x19, 0xBF);
            
            apu_write(&apu, 0x1A, 0x7F);
            apu_write(&apu, 0x1B, 0x00);
            apu_write(&apu, 0x1C, 0x9F);
            apu_write(&apu, 0x1D, 0x00);
            apu_write(&apu, 0x1E, 0xBF);
            
            apu_write(&apu, 0x20, 0x00);
            apu_write(&apu, 0x21, 0x00);
            apu_write(&apu, 0x22, 0x00);
            apu_write(&apu, 0x23, 0xBF);
            
            if(IS_CGB)
            {
                mb.reg.A = 0x11;
                mb.reg.BC = 0x0000;
                mb.reg.DE = 0xFF56;
                mb.reg.HL = 0x000D;
                mb.reg.F = 0x80;
                
                FILE* f = 0;
                #if !CONFIG_NOBOOTMEME
                //f = fopen("../testrom_cgb.gbc", "rb");
                #error no
                #endif
                if(f)
                {
                    //TODO: bad memory leak
                    r8* asd = malloc(0x1000);
                    
                    fread(asd, 0x900, 1, f);
                    fclose(f);
                    
                    //memcpy(&asd[0x100], &dis.ROM[0x100], 0x50);
                    
                    //mb.micache.mc_read = asd;
                    //mb.micache.mc_execute = asd;
                    //mb.micache.r_read = 0;
                    //mb.micache.r_execute = 0;
                    
                    mb.PC = 0;
                    mb.DIV = 0;
                    
                    pp.rLCDC = 0;
                    pp.rSTAT = 0;
                    
                    #if CONFIG_DBG
                    _IS_DBG = 0;
                    #endif
                }
            }
            else
            {
                FILE* f = 0;
                #if !CONFIG_NOBOOTMEME
                f = fopen("../testrom.gb", "rb");
                #error no
                #endif
                if(f)
                {
                    fread(&dis.WRAM[0x6000], 256, 1, f);
                    fclose(f);
                    
                    //memcpy(&dis.WRAM[0x6100], &dis.ROM[0x100], 0x50);
                    
                    //mb.micache.mc_read = &dis.WRAM[0x6000];
                    //mb.micache.mc_execute = &dis.WRAM[0x6000];
                    //mb.micache.r_read = 0;
                    //mb.micache.r_execute = 0;
                    
                    mb.PC = 0;
                    mb.DIV = 0;
                    
                    pp.rLCDC = 0;
                    pp.rSTAT = 0;
                    
                    #if CONFIG_DBG
                    _IS_DBG = 0;
                    #endif
                }
            }
        }
        
        userdata.mb = &mb;
        userdata.ppu = &pp;
        userdata.apu = &apu;
        
        userdata.TIMER_CNT = 0;
        userdata.JOYP_RAW = 0;
        
        dis.N_ROM = fs >> 14;
        dis.N_SRAM = 4;
        dis.userdata = &userdata;
        dis.dispatch_IO = cb_IO;
        dis.dispatch_ROM = cb_ROM;
    }
    
    HWND wnd = wnd_create(&userdata);
    if(!wnd)
    {
        puts("Failed to create main window");
        return 1;
    }
    
    #if CONFIG_APU_ENABLE
    {
        ZeroMemory(&wavfmt1, sizeof(wavfmt1));
        wavfmt1.wFormatTag = WAVE_FORMAT_PCM;
        wavfmt1.nChannels = 2;
        wavfmt1.nSamplesPerSec = 131072;
        wavfmt1.wBitsPerSample = 16;
        wavfmt1.cbSize = 0;
        wavfmt1.nBlockAlign = (wavfmt1.wBitsPerSample * wavfmt1.nChannels) >> 3;
        wavfmt1.nAvgBytesPerSec = wavfmt1.nBlockAlign * wavfmt1.nSamplesPerSec;
        
        MMRESULT res = waveOutOpen(&hwow, 0, &wavfmt1, 0, 0, 0);
        if(res)
        {
            hwow = 0;
            printf("waveOutOpen error: %i\n", res);
            puts("There will be no sound.");
        }
        
        waveOutReset(hwow);
        
        DWORD bufsize = wavfmt1.nBlockAlign * 4096;
        ZeroMemory(bufo, sizeof(bufo));
        
        for(i = 0; i != cnto; i++)
        {
            bufo[i].dwBufferLength = bufsize;
            bufo[i].lpData = malloc(bufsize * 2);
            
            waveOutPrepareHeader(hwow, bufo + i, sizeof(WAVEHDR));
        }
    }
    #endif
    
    #if CONFIG_VSYNC == 1
    dwm = LoadLibraryA("dwmapi");
    if(dwm)
    {
        DwmFlush = (void*)GetProcAddress(dwm, "DwmFlush");
        if(!DwmFlush)
        {
            puts("! No DwmFlush");
            FreeLibrary(dwm);
            dwm = 0;
        }
    }
    #elif CONFIG_VSYNC == 2
    mmidi_reset();
    #endif
    
    #if CONFIG_TAS
    FILE* ftas = fopen("TAS.bin",
    #if CONFIG_TAS == 2
        "rb"
    #else
        "wb"
    #endif
        );
    if(!ftas)
    {
        puts("Failed to access TAS file");
        goto kil_end;
    }
    
    #if CONFIG_TAS == 2
    fread(&userdata.JOYP_RAW, 1, 1, ftas);
    #else
    if(!wnd_update(wnd))
        goto kil_end;
    
    fwrite(&userdata.JOYP_RAW, 1, 1, ftas);
    #endif
    #endif
    
    for(;;)
    {
        int lol = 0;
        
        /*
        if(mb.PC == 0x08)
        {
            mb.IR.raw = 0x00;
            mb.PC = 0x0C;
        }
        */
        
        //if(mb.PC == 0xA1)
        if(0)
        {
            lol = 1;
        }
        
        if(lol)
            regdump(&mb);
        
        word cycles;
        
        if(!mb.HALTING || mbh_irq_get_pending(&mb))
        {
            if(mb.HALTING)
            {
                //printf("! UNHALT         | IE=%02X (IF=%02X) | LCDC=%02X STAT=%02X LYC=%02X LY=%02X\n", mb.IE, mb.IF & 0x1F, pp.rLCDC, pp.rSTAT, pp.rLYC, pp.state.scanY);
                
                mb.HALTING = 0;
            }
            
            cycles = mb_exec(&mb);
        }
        else
        {
            cycles = 1;
        }
        //printf("  Took %u cycles.\n", cycles);
        
    #if CONFIG_DOCTOR
        dbg_dump(&mb);
    #endif
        
        if(lol)
        {
            lol = 0;
            regdump(&mb);
        }
        
        if(mb.HALTING && !mb.IR.high)
        {
            //printf("! HALT @ %02X:%04X | IE=%02X (IF=%02X) | LCDC=%02X STAT=%02X LYC=%02X LY=%02X\n", mb.mi->BANK_ROM, mb.PC, mb.IE, mb.IF & 0x1F, pp.rLCDC, pp.rSTAT, pp.rLYC, pp.state.scanY);
            //Sleep(1000);
            mb.IR.high = 1;
        }
        
        if(!cycles)
        {
            puts("Core break!");
            
            printf("- IR: %02X\n", mb.IR.raw);
            
            regdump(&mb);
            
            if(pp.rLCDC & 0x80)
            {
                while(pp.state.scanY != 144) ppu_tick(&pp, 456);
                while(pp.state.scanY == 144) ppu_tick(&pp, 456);
                while(pp.state.scanY != 144) ppu_tick(&pp, 456);
                while(pp.state.scanY == 144) ppu_tick(&pp, 456);
                while(pp.state.scanY != 144) ppu_tick(&pp, 456);
                while(pp.state.scanY == 144) ppu_tick(&pp, 456);
                
                wnd_blit(wnd, &pp);
                wnd_update(wnd);
            }
            
            /*
            var k,v;
            
            for(v = 0; v != 0x19; ++v)
            {
                printf("%02X: ", v);
                
                const r8* __restrict shit = &pp.VRAM[v << 4];
                
                for(k = 0; k != 16; ++k)
                {
                    printf(" %02X", shit[k]);
                }
                
                puts("");
            }
            
            for(v = 0; v != 32; ++v)
            {
                for(k = 0; k != 32; ++k)
                {
                    var val = pp.VRAM[0x1800 + (v * 32) + k];
                    
                    if(val)
                        printf(" %02X", val);
                    else
                        printf(" %c%c", '-', '-');
                }
                
                puts("");
            }
            */
            
            wnd_update_loop(wnd);
            break;
        }
        
        //TODO: 17556 samples per frame
        
        mb.DIV += cycles;
        pgf_timer_update(&userdata, cycles);
        
        #if CONFIG_APU_ENABLE
        if(hwow)
        {
            apu_tick(&apu, cycles);
            
            var k = 0;
            j = apu.outbuf_pos >> 1;
            i = 0;
            
            while(j)
            {
                LPWAVEHDR ohdr = bufo + toggo;
                volatile DWORD* flags = &ohdr->dwFlags;
                
                /*
                if((*flags & 0x11) == 0x10)
                {
                    //printf("Blocked   %lu\n", toggo);
                    
                    //anything = 0;
                    
                    do
                    {
                        int64_t dumb = -1;
                        NtDelayExecution(1, &dumb);
                    }
                    while((*flags & 0x11) == 0x10);
                    
                    //printf("Unblocked %lu\n", toggo);
                }
                */
                
                //if(anything)
                {
                    //printf("Buffer pos %lu\n", hdr->dwBytesRecorded);
                    
                    s16* __restrict lpptr = (s16* __restrict)((r8*)ohdr->lpData + ohdr->dwBytesRecorded);
                    
                    for(;;)
                    {
                        if(ohdr->dwBytesRecorded >= ohdr->dwBufferLength)
                            break;
                        
                        s32 sum_l = 0;
                        s32 sum_r = 0;
                        
                        //for(l = 0; l != 8; l++)
                        {
                            sum_l += apu.outbuf[k++];
                            sum_r += apu.outbuf[k++];
                        }
                        
                        //sum_l /= 8;
                        //sum_r /= 8;
                        
                        *(lpptr++) = (s16)sum_l;
                        *(lpptr++) = (s16)sum_r;
                        
                        ohdr->dwBytesRecorded += 4;
                        
                        if(!--j)
                        {
                            apu.outbuf_pos = 0;
                            break;
                        }
                    }
                    
                    
                    
                    if(ohdr->dwBytesRecorded >= ohdr->dwBufferLength)
                    {
                        ohdr->dwBytesRecorded = ohdr->dwBufferLength;
                        
                        HRESULT res;
                        for(;;)
                        {
                            res = waveOutWrite(hwow, ohdr, sizeof(WAVEHDR));
                            
                            if(!res)
                                break;
                            
                            int64_t dumb = -1;
                            NtDelayExecution(1, &dumb);
                        }
                        
                        if(++toggo >= cnto)
                            toggo = 0;
                    }
                }
            }
        }
        #endif
        
        if(pp.rLCDC & 0x80)
        {
            var ppirq = ppu_tick(&pp, cycles << 2);
            if(ppirq)
            {
                //printf("  - IF_SCHED %02X\n", ppirq);
                mb.IF |= ppirq;
            }
            
            if(pp._redrawed)
            {
                pp._redrawed = 0;
                
                wnd_blit(wnd, &pp);
                if(!wnd_update(wnd))
                    break;
                
            #if CONFIG_TAS
            #if CONFIG_TAS == 2
            if(!fread(&userdata.JOYP_RAW, 1, 1, ftas))
                break;
            #else
            fwrite(&userdata.JOYP_RAW, 1, 1, ftas);
            #endif
            #endif
                
                #if CONFIG_VSYNC == 1
                if(DwmFlush)
                    DwmFlush();
                #elif CONFIG_VSYNC == 2
                mmidi_sleep((int64_t)(1e7 / 4194304.0 * (456.0 * 154.0)));
                #endif
            }
        }
        else
        {
            if(!wnd_update(wnd))
                break;
        }
    }
    
#if CONFIG_TAS
kil_end:
    fclose(ftas);
#endif
    if(wnd)
    {
        DestroyWindow(wnd);
    }
    
    #if CONFIG_VSYNC == 1
    if(dwm)
        FreeLibrary(dwm);
    #elif CONFIG_VSYNC == 2
    #endif
    
    if(ntdll)
        FreeLibrary(ntdll);
    
    return 0;
}
