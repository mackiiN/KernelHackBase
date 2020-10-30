PVOID ResolveWin32k(const char* Name)
{
    //win32kfull
    auto win32kFull = GetKernelModuleBase("win32kfull.sys");
    auto func = GetProcAdress(win32kFull, Name);

    //win32kbase
    if (!func) {
        auto win32kBase = GetKernelModuleBase("win32kbase.sys");
        func = GetProcAdress(win32kBase, Name);
    }

    //win32k
    if (!func) {
        auto win32k = GetKernelModuleBase("win32k.sys");
        func = GetProcAdress(win32k, Name);
    }

    return func;
}

template<typename Ret = void, typename... Stack>
Ret __forceinline CallPtr(PVOID Fn, Stack... Args) {
    typedef Ret(*ShellFn)(Stack...);
    return ((ShellFn)Fn)(Args...);
}

enum PolyMode {
    Polygon = 1,
    PolyLine = 2
};

HDC GetDC(HWND hWnd)
{
    static PVOID GetDC_Fn = 0;
    if (!GetDC_Fn) {
        GetDC_Fn = ResolveWin32k("NtUserGetDC");
    }

    return CallPtr<HDC>(GetDC_Fn, hWnd);
}

HDC CreateCompatibleDC(HDC hDC)
{
    static PVOID CreateCompatibleDC_Fn = 0;
    if (!CreateCompatibleDC_Fn) {
        CreateCompatibleDC_Fn = ResolveWin32k("NtGdiCreateCompatibleDC");
    }

    return CallPtr<HDC>(CreateCompatibleDC_Fn, hDC);
}

void DeleteObject(HGDIOBJ Obj)
{
    static PVOID DeleteObject_Fn = 0;
    if (!DeleteObject_Fn) {
        DeleteObject_Fn = ResolveWin32k("NtGdiDeleteObjectApp");
    }

    CallPtr(DeleteObject_Fn, Obj);
}

HBITMAP NtGdiSelectBitmap(HDC hDC, HBITMAP hBitmap)
{
    static PVOID NtGdiSelectBitmap_Fn = 0;
    if (!NtGdiSelectBitmap_Fn) {
        NtGdiSelectBitmap_Fn = ResolveWin32k("NtGdiSelectBitmap");
    }

    return CallPtr<HBITMAP>(NtGdiSelectBitmap_Fn, hDC, hBitmap);
}

HBRUSH NtGdiSelectBrush(HDC hDC, HBRUSH hBrush)
{
    static PVOID NtGdiSelectBrush_Fn = 0;
    if (!NtGdiSelectBrush_Fn) {
        NtGdiSelectBrush_Fn = ResolveWin32k("NtGdiSelectBrush");
    }

    return CallPtr<HBRUSH>(NtGdiSelectBrush_Fn, hDC, hBrush);
}

HPEN NtGdiSelectPen(HDC hDC, HPEN hPen)
{
    static PVOID NtGdiSelectPen_Fn = 0;
    if (!NtGdiSelectPen_Fn) {
        NtGdiSelectPen_Fn = ResolveWin32k("NtGdiSelectPen");
    }

    return CallPtr<HPEN>(NtGdiSelectPen_Fn, hDC, hPen);
}

HPEN CreatePen(int Style, int Width, COLORREF Color)
{
    static PVOID NtGdiCreatePen_Fn = 0;
    if (!NtGdiCreatePen_Fn) {
        NtGdiCreatePen_Fn = ResolveWin32k("NtGdiCreatePen");
    }

    return CallPtr<HPEN>(NtGdiCreatePen_Fn, Style, Width, Color, 0);
}

HBRUSH CreateSolidBrush(COLORREF Color)
{
    static PVOID NtGdiCreateSolidBrush_Fn = 0;
    if (!NtGdiCreateSolidBrush_Fn) {
        NtGdiCreateSolidBrush_Fn = ResolveWin32k("NtGdiCreateSolidBrush");
    }

    return CallPtr<HBRUSH>(NtGdiCreateSolidBrush_Fn, Color, 0);
}

HBITMAP CreateDIBSection(HDC hDC, PVOID Info, UINT Flags, PVOID* MappedBitmap, HANDLE Section, DWORD Offset)
{
    static PVOID NtGdiCreateDIBSection_Fn = 0;
    if (!NtGdiCreateDIBSection_Fn) {
        NtGdiCreateDIBSection_Fn = ResolveWin32k("NtGdiCreateDIBSection");
    }

    return CallPtr<HBITMAP>(NtGdiCreateDIBSection_Fn, hDC, Section, Offset, Info, Flags, /*info size*/40, 0, 0, MappedBitmap);
}

void NtGdiPolyPolyDraw(HDC hDC, POINT* Dots, ULONG* NumDots, PolyMode Mode)
{
    static PVOID NtGdiPolyPolyDraw_Fn = 0;
    if (!NtGdiPolyPolyDraw_Fn) {
        NtGdiPolyPolyDraw_Fn = ResolveWin32k("NtGdiPolyPolyDraw");
    }

    CallPtr(NtGdiPolyPolyDraw_Fn, hDC, Dots, NumDots, 1, Mode);
}

typedef struct tagRGBQUAD {
    BYTE    rgbBlue;
    BYTE    rgbGreen;
    BYTE    rgbRed;
    BYTE    rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFOHEADER {
    DWORD      biSize;
    LONG       biWidth;
    LONG       biHeight;
    WORD       biPlanes;
    WORD       biBitCount;
    DWORD      biCompression;
    DWORD      biSizeImage;
    LONG       biXPelsPerMeter;
    LONG       biYPelsPerMeter;
    DWORD      biClrUsed;
    DWORD      biClrImportant;
} BITMAPINFOHEADER, FAR* LPBITMAPINFOHEADER, * PBITMAPINFOHEADER;

typedef struct tagBITMAPINFO {
    BITMAPINFOHEADER    bmiHeader;
    RGBQUAD             bmiColors[1];
} BITMAPINFO, FAR* LPBITMAPINFO, * PBITMAPINFO;

#define RGB(r,g,b) ((COLORREF)(((BYTE)(r)|((WORD)((BYTE)(g))<<8))|(((DWORD)(BYTE)(b))<<16)))

//render
class Render
{
private:
    HDC ScreenDC;
    HPEN NullPen;
    int CurrentWidth;
    int CurrentHeight;
    PBYTE MappedTexture;
    HBITMAP ScreenBitmap;

    //init render
    void Init(int Width, int Height)
    {
        //create dc & null pen
        NullPen = CreatePen(5, 0, 0);
        ScreenDC = CreateCompatibleDC(nullptr);

        //create bitmap
        PBITMAPINFO InfoUser = (PBITMAPINFO)UAlloc(4096);
        InfoUser->bmiHeader.biSize = 40;
        InfoUser->bmiHeader.biWidth = Width;
        InfoUser->bmiHeader.biHeight = -Height;
        InfoUser->bmiHeader.biPlanes = 1;
        InfoUser->bmiHeader.biBitCount = 32;
        InfoUser->bmiHeader.biCompression = 0;
        InfoUser->bmiHeader.biSizeImage = Width * Height * 4;
        PVOID* MappedTextureUser = (PVOID*)((ULONG64)InfoUser + 0x800);
        ScreenBitmap = CreateDIBSection(ScreenDC, InfoUser, 0, MappedTextureUser, nullptr, 0);
        MappedTexture = (PBYTE)*MappedTextureUser;
        UFree(InfoUser);

        //select backbuffer
        RemoveObj(NtGdiSelectBitmap(ScreenDC, ScreenBitmap));

        //save vars
        CurrentWidth = Width;
        CurrentHeight = Height;

        //dbg
        ScreenDC = GetDC(0);
    }

    void PolyLineInternal(POINT* Dots, ULONG64 NumDots, int Thick, COLORREF Color)
    {
        auto Pen = CreatePen(0, Thick, Color);
        NtGdiSelectPen(ScreenDC, Pen);
        static PVOID GrePolyPolyline = 0;
        if (!GrePolyPolyline)
            GrePolyPolyline = ResolveWin32k("GrePolyPolyline");
        CallPtr(GrePolyPolyline, ScreenDC, Dots, &NumDots, 1ull, NumDots);
        DeleteObject(Pen);
    }

    void PolygonInternal(POINT* Dots, ULONG64 NumDots, COLORREF Color)
    {
        auto Brush = CreateSolidBrush(Color);
        NtGdiSelectPen(ScreenDC, NullPen);
        NtGdiSelectBrush(ScreenDC, Brush);
        static PVOID GrePolyPolygon = 0;
        if (!GrePolyPolygon)
            GrePolyPolygon = ResolveWin32k("GrePolyPolygon");
        CallPtr(GrePolyPolygon, ScreenDC, Dots, &NumDots, 1ull, NumDots);
        DeleteObject(Brush);
    }

    __forceinline void RemoveObj(HGDIOBJ Obj) {
        if (Obj) DeleteObject(Obj);
    }

public:
    //mgr
    __forceinline void NewFrame(int Width, int Height) {
        if ((Width != CurrentWidth) || (Height != CurrentHeight)) {
            RemoveObj(ScreenBitmap);
            RemoveObj(NullPen);
            Init(Width, Height);
        }
    }

    void EndFrame(PBYTE Buffer)
    {
        //flush all batches
        //GdiFlush();

        //fix alpha & copy & clear buffer (BUG: no black color)
        for (ULONG i = 0; i < CurrentWidth * CurrentHeight * 4; i += 4)
        {
            //copy pixel
            ULONG Pixel = *(ULONG*)&Buffer[i] = *(ULONG*)&MappedTexture[i];

            //fix alpha & reset pixel
            if (Pixel) Buffer[i + 3] = 0xFF;
            *(ULONG*)&MappedTexture[i] = 0;
        }
    }

    //render
    void Line(int x0, int y0, int x1, int y1, COLORREF Color, int Thick = 1)
    {
        //gen dots
        POINT Dots[2];
        Dots[0] = { x0, y0 };
        Dots[1] = { x1, y1 };

        //draw polyline
        PolyLineInternal(Dots, 2, Thick, Color);
    }

    void Rectangle(int x, int y, int w, int h, COLORREF Color, int Thick = 1)
    {
        //gen dots
        POINT Dots[5];
        Dots[0] = { x, y };
        Dots[1] = { x + w, y };
        Dots[2] = { x + w, y + h };
        Dots[3] = { x, y + h };
        Dots[4] = Dots[0];

        //draw polyline
        PolyLineInternal(Dots, 5, Thick, Color);
    }

    void FillRectangle(int x, int y, int w, int h, COLORREF Color)
    {
        //gen dots
        POINT Dots[4];
        Dots[0] = { x, y };
        Dots[1] = { x + w, y };
        Dots[2] = { x + w, y + h };
        Dots[3] = { x, y + h };

        //draw polygon
        PolygonInternal(Dots, 4, Color);
    }
};
