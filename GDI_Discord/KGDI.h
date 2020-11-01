#include <wingdi.h>

PVOID ResolveWin32k(const char* Name)
{
    //win32kfull
    auto win32kFull = GetKernelModuleBase(E("win32kfull.sys"));
    auto func = GetProcAdress(win32kFull, Name);

    //win32kbase
    if (!func) {
        auto win32kBase = GetKernelModuleBase(E("win32kbase.sys"));
        func = GetProcAdress(win32kBase, Name);
    }

    //win32k
    if (!func) {
        auto win32k = GetKernelModuleBase(E("win32k.sys"));
        func = GetProcAdress(win32k, Name);
    }

    return func;
}

//render
class Render
{
private:
    HDC ScreenDC;
    HPEN NullPen;
    int ScrScale;
    PVOID UserBuffer;

    int CurrentWidth;
    int CurrentHeight;
    PBYTE MappedTexture;
    HBITMAP ScreenBitmap;
    PBYTE ScaleMappedTexture;
    HBITMAP ScaleScreenBitmap;

    //internals
    HPEN CreatePenInternal(int Style, int Width, COLORREF Color)
    {
        static PVOID NtGdiCreatePen_Fn = nullptr;
        if (!NtGdiCreatePen_Fn) {
            NtGdiCreatePen_Fn = EPtr(ResolveWin32k(E("NtGdiCreatePen")));
        }

        return CallPtr<HPEN>(EPtr(NtGdiCreatePen_Fn), Style, Width, Color, 0);
    }

    _FI HGDIOBJ GetStockObjectInternal(ULONG Index) {
        auto PEB = PsGetProcessPeb(IoGetCurrentProcess());
        auto GdiSharedHandleTable = *(ULONG64*)((ULONG64)PEB + 0xF8/*GdiSharedHandleTable*/);
        const auto ObjArray = (ULONG64*)(GdiSharedHandleTable + 0x1800B0/*in GetStockObject*/);
        return (HGDIOBJ)ObjArray[Index];
    }

    HBRUSH SelectBrushInternal(HDC hDC, HBRUSH hBrush)
    {
        static PVOID NtGdiSelectBrush_Fn = nullptr;
        if (!NtGdiSelectBrush_Fn) {
            NtGdiSelectBrush_Fn = EPtr(ResolveWin32k(E("NtGdiSelectBrush")));
        }

        return CallPtr<HBRUSH>(EPtr(NtGdiSelectBrush_Fn), hDC, hBrush);
    }

    HPEN SelectPenInternal(HDC hDC, HPEN hPen)
    {
        static PVOID NtGdiSelectPen_Fn = nullptr;
        if (!NtGdiSelectPen_Fn) {
            NtGdiSelectPen_Fn = EPtr(ResolveWin32k(E("NtGdiSelectPen")));
        }

        return CallPtr<HPEN>(EPtr(NtGdiSelectPen_Fn), hDC, hPen);
    }

    void RemoveObjInternal(HGDIOBJ Obj)
    {
        if (Obj)
        {
            static PVOID DeleteObject_Fn = nullptr;
            if (!DeleteObject_Fn) {
                DeleteObject_Fn = EPtr(ResolveWin32k(E("NtGdiDeleteObjectApp")));
            }

            CallPtr(EPtr(DeleteObject_Fn), Obj);
        }
    }

    void PolyLineInternal(POINT* Dots, ULONG64 NumDots, int Thick, COLORREF Color)
    {
        //decrt DC
        auto hDC = EPtr(ScreenDC);

        //create pen
        auto Pen = CreatePenInternal(0/*PS_SOLID*/, Thick, Color);

        //select color
        SelectPenInternal(hDC, Pen);

        //fill line
        static PVOID GrePolyPolyline = nullptr;
        if (!GrePolyPolyline)
            GrePolyPolyline = EPtr(ResolveWin32k(E("GrePolyPolyline")));
        CallPtr(EPtr(GrePolyPolyline), hDC, Dots, &NumDots, 1ull, NumDots);

        //cleanup
        RemoveObjInternal(Pen);
    }

    void PolygonInternal(POINT* Dots, ULONG64 NumDots, COLORREF Color)
    {
        //decrt DC
        auto hDC = EPtr(ScreenDC);

        //NtGdiCreateSolidBrush
        static PVOID NtGdiCreateSolidBrush_Fn = 0;
        if (!NtGdiCreateSolidBrush_Fn) {
            NtGdiCreateSolidBrush_Fn = EPtr(ResolveWin32k(E("NtGdiCreateSolidBrush")));
        }

        //create brush
        auto Brush = CallPtr<HBRUSH>(EPtr(NtGdiCreateSolidBrush_Fn), Color, 0);

        //select colors
        SelectPenInternal(hDC, EPtr(NullPen));
        SelectBrushInternal(hDC, Brush);

        //fill polygon
        static PVOID GrePolyPolygon = nullptr;
        if (!GrePolyPolygon)
            GrePolyPolygon = EPtr(ResolveWin32k(E("GrePolyPolygon")));
        CallPtr(EPtr(GrePolyPolygon), hDC, Dots, &NumDots, 1ull, NumDots);

        //cleanup
        RemoveObjInternal(Brush);
    }

    HBITMAP SelectBitMapInternal(HBITMAP BitMap)
    {
        static PVOID NtGdiSelectBitmap_Fn = nullptr;
        if(!NtGdiSelectBitmap_Fn){
            NtGdiSelectBitmap_Fn = EPtr(ResolveWin32k(E("NtGdiSelectBitmap")));
        }
        
        return CallPtr<HBITMAP>(EPtr(NtGdiSelectBitmap_Fn), EPtr(ScreenDC), BitMap);
    }

    //Fast Math
    _FI float CosAdd(float x) {
        float x2 = x * x;
        const float c1 = 0.99940307f;
        const float c2 = -0.49558072f;
        const float c3 = 0.03679168f;
        return (c1 + x2 * (c2 + c3 * x2));
    }

    _FI float FastSqrt(float x) {
        union { int i; float x; } u; u.x = x;
        u.i = (1 << 29) + (u.i >> 1) - (1 << 22);
        return u.x;
    }

    _FI float FastCos(float angle) {
        angle = angle - floorf(angle * 0.15f) * 6.28f;
        angle = angle > 0.f ? angle : -angle;
        if (angle < 1.57f) return CosAdd(angle);
        if (angle < 3.14f) return -CosAdd(3.14f - angle);
        if (angle < 4.71f) return -CosAdd(angle - 3.14f);
        return CosAdd(6.28f - angle);
    }

    _FI float FastSin(float angle) {
        return FastCos(1.57f - angle);
    }

public:
    //mgr
    void Init(int Width, int Height, int Scale)
    {
        //create dc
        PVOID CreateCompatibleDC_Fn = ResolveWin32k(E("NtGdiCreateCompatibleDC"));
        ScreenDC = EPtr(CallPtr<HDC>(CreateCompatibleDC_Fn, nullptr));

        //get null pen & null brush
        NullPen = (HPEN)EPtr(GetStockObjectInternal(8/*NULL_PEN*/));

        //alloc usermode buff
        UserBuffer = EPtr(UAlloc(4096));

        //create bitmap (org size)
        PBITMAPINFO InfoUser = (PBITMAPINFO)EPtr(UserBuffer);
        InfoUser->bmiHeader.biSize = 40;
        InfoUser->bmiHeader.biWidth = Width;
        InfoUser->bmiHeader.biHeight = -Height;
        InfoUser->bmiHeader.biPlanes = 1;
        InfoUser->bmiHeader.biBitCount = 32;
        InfoUser->bmiHeader.biCompression = 0;
        InfoUser->bmiHeader.biSizeImage = Width * Height * 4;
        PVOID* MappedTextureUser = (PVOID*)((ULONG64)InfoUser + 0x800);
        PVOID NtGdiCreateDIBSection_Fn = ResolveWin32k(E("NtGdiCreateDIBSection"));
        ScreenBitmap = EPtr(CallPtr<HBITMAP>(NtGdiCreateDIBSection_Fn, EPtr(ScreenDC), nullptr, 0, InfoUser, 0, 40, 0, 0, MappedTextureUser));
        MappedTexture = EPtr((PBYTE)*MappedTextureUser);

        //add scale
        if (Scale > 1)
        {
            //create bitmap (scale size)
            InfoUser->bmiHeader.biWidth = Width * Scale;
            InfoUser->bmiHeader.biHeight = -(Height * Scale);
            InfoUser->bmiHeader.biSizeImage = (Width * Scale) * (Height * Scale) * 4;
            ScaleScreenBitmap = EPtr(CallPtr<HBITMAP>(NtGdiCreateDIBSection_Fn, EPtr(ScreenDC), nullptr, 0, InfoUser, 0, 40, 0, 0, MappedTextureUser));
            ScaleMappedTexture = EPtr((PBYTE)*MappedTextureUser);
            
            //GreSetStretchBltMode
            auto win32kFull = GetKernelModuleBase(E("win32kfull.sys"));
            auto GreSetStretchBltMode = (PVOID)RVA(FindPatternSect(win32kFull, E(".text"), E("E8 ? ? ? ? 48 8B D7 89 84")), 5);
            CallPtr(GreSetStretchBltMode, EPtr(ScreenDC), HALFTONE);
        } 
        
        //cleanup 
        MemZero(InfoUser, 0x808/*https://en.wikipedia.org/wiki/808_Mafia*/);

        //select backbuffer
        RemoveObjInternal(SelectBitMapInternal(EPtr(((Scale > 1) ? ScaleScreenBitmap : ScreenBitmap))));

        //save vars
        ScrScale = Scale;
        CurrentWidth = Width;
        CurrentHeight = Height;
    }

    _FI void NewFrame(int Width, int Height, int Scale = 1) {
        if ((Width != CurrentWidth) || (Height != CurrentHeight)) 
        {
            //cleanup
            Release();

            //setup render
            Init(Width, Height, Scale);
        }
    }

    void EndFrame(PBYTE Buffer)
    {
        //apply extrasampling (if need)
        if (ScrScale > 1)
        {
            //select original bitmap
            auto SBitMap = SelectBitMapInternal(EPtr(ScreenBitmap));

            //NtGdiStretchDIBitsInternal
            static PVOID NtGdiStretchDIBitsInternal_Fn = 0;
            if (!NtGdiStretchDIBitsInternal_Fn) {
                NtGdiStretchDIBitsInternal_Fn = EPtr(ResolveWin32k(E("NtGdiStretchDIBitsInternal")));
            }

            //resize bitmap
            PBITMAPINFO InfoUser = (PBITMAPINFO)EPtr(UserBuffer);
            InfoUser->bmiHeader.biSize = 40;
            InfoUser->bmiHeader.biWidth = (CurrentWidth * ScrScale);
            InfoUser->bmiHeader.biHeight = -(CurrentHeight * ScrScale);
            InfoUser->bmiHeader.biPlanes = 1;
            InfoUser->bmiHeader.biBitCount = 32;
            InfoUser->bmiHeader.biCompression = 0;
            InfoUser->bmiHeader.biSizeImage = (CurrentWidth * ScrScale) * (CurrentHeight * ScrScale) * 4;
            CallPtr(EPtr(NtGdiStretchDIBitsInternal_Fn), EPtr(ScreenDC), 0, 0, CurrentWidth, CurrentHeight, 0, 0, CurrentWidth * ScrScale, CurrentHeight * ScrScale, EPtr(ScaleMappedTexture), InfoUser, 0, SRCCOPY, 40, InfoUser->bmiHeader.biSizeImage, 0ull);
            
            //cleanup & restore bitmap
            MemZero(InfoUser, sizeof(BITMAPINFO));
            SelectBitMapInternal(SBitMap);
        }
        
        //fix alpha & copy & clear buffer (BUG: no black color)
        auto MappedTextureDecrt = EPtr(MappedTexture);
        for (ULONG i = 0; i < CurrentWidth * CurrentHeight * 4; i += 8/*2 pixels*/)
        {
            //copy pixels
            ULONG Pixel1 = *(ULONG*)&Buffer[i] = *(ULONG*)&MappedTextureDecrt[i];
            ULONG Pixel2 = *(ULONG*)&Buffer[i + 4] = *(ULONG*)&MappedTextureDecrt[i + 4];

            //fix alpha
            if (Pixel1) Buffer[i + 3] = 0xFF;
            if (Pixel2) Buffer[i + 7] = 0xFF;

            //reset pixels
            *(ULONG*)&MappedTextureDecrt[i] = 0;
            *(ULONG*)&MappedTextureDecrt[i + 4] = 0;
        }
    }

    void Release() {
        RemoveObjInternal(EPtr(ScaleScreenBitmap));
        RemoveObjInternal(EPtr(ScreenBitmap));
        RemoveObjInternal(EPtr(ScreenDC));
        UFree(UserBuffer);
    }

    //render line
    void Line(int x0, int y0, int x1, int y1, COLORREF Color, int Thick = 1)
    {
        //apply scale
        x0 *= ScrScale;
        y0 *= ScrScale;
        x1 *= ScrScale;
        y1 *= ScrScale;
        Thick *= ScrScale;

        //gen dots
        POINT Dots[2];
        Dots[0] = { x0, y0 };
        Dots[1] = { x1, y1 };

        //draw polyline
        PolyLineInternal(Dots, 2, Thick, Color);
    }

    //render circle
    void Circle(int x, int y, COLORREF Color, float Radius, int Thick = 1)
    {
        //apply scale
        x *= ScrScale;
        y *= ScrScale;
        Thick *= ScrScale;
        Radius *= ScrScale;

        //gen dots
        POINT Dots[120]; int NumDots = 0;
        for (float i = 0.f; i < 6.28f; i += .054f) {
            Dots[NumDots++] = {
                LONG(x + Radius * FastCos(i)),
                LONG(y + Radius * FastSin(i))
            };
        }

        //fix end
        Dots[NumDots] = Dots[0];

        //draw polyline
        PolyLineInternal(Dots, NumDots, Thick, Color);
    }

    void FillCircle(int x, int y, COLORREF Color, float Radius)
    {
        //apply scale
        x *= ScrScale;
        y *= ScrScale;
        Radius *= ScrScale;

        //gen dots
        POINT Dots[120]; int NumDots = 0;
        for (float i = 0.f; i < 6.28f; i += .054f) {
            Dots[NumDots++] = {
                LONG(x + Radius * FastCos(i)),
                LONG(y + Radius * FastSin(i))
            };
        }

        //draw polygon
        PolygonInternal(Dots, NumDots, Color);
    }

    //render rectangle
    void Rectangle(int x, int y, int w, int h, COLORREF Color, int Thick = 1)
    { 
        //apply scale
        x *= ScrScale;
        y *= ScrScale;
        w *= ScrScale;
        h *= ScrScale;
        Thick *= ScrScale;

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
    
    void RoundedRectangle(int x, int y, int w, int h, COLORREF Color, float Radius, int Thick = 1) //shit
    {
        //apply scale
        x *= ScrScale;
        y *= ScrScale;
        w *= ScrScale;
        h *= ScrScale;
        Thick *= ScrScale;
        Radius *= ScrScale;

        //gen dots
        POINT Add{};
        POINT Dots[25];
        for (int i = 0; i < 24; ++i)
        {
            //gen dot
            float angle = (float(i) / -24.f) * 6.28f - (6.28f / 16.f);
            Dots[i].x = Radius + x + Add.x + (Radius * FastSin(angle));
            Dots[i].y = h - Radius + y + Add.y + (Radius * FastCos(angle));

            //calc offset
            if (i == 4) { Add.y = -h + (Radius * 2.f); }
            else if (i == 10) { Add.x = w - (Radius * 2.f); }
            else if (i == 16) Add.y = 0.f; else if (i == 22) Add.x = 0.f;
        }

        //fix end
        Dots[24] = Dots[0];

        //Line(Dots[16].x, Dots[16].y, Dots[22].x, Dots[22].y, RGB(255, 0, 0));

        //draw polyline
        PolyLineInternal(Dots, 25, Thick, Color);
    }

    void FillRectangle(int x, int y, int w, int h, COLORREF Color)
    {
        //apply scale
        x *= ScrScale;
        y *= ScrScale;
        w *= ScrScale;
        h *= ScrScale;

        //gen dots
        POINT Dots[4];
        Dots[0] = { x, y };
        Dots[1] = { x + w, y };
        Dots[2] = { x + w, y + h };
        Dots[3] = { x, y + h };

        //draw polygon
        PolygonInternal(Dots, 4, Color);
    }

    void FillRoundedRectangle(int x, int y, int w, int h, COLORREF Color, float Radius)
    {
        //apply scale
        x *= ScrScale;
        y *= ScrScale;
        w *= ScrScale;
        h *= ScrScale; 
        Radius *= ScrScale;

        //gen dots
        POINT Add{};
        POINT Dots[24];
        for (int i = 0; i < 24; ++i)
        {
            //gen dot
            float angle = (float(i) / -24.f) * 6.28f - (6.28f / 16.f);
            Dots[i].x = Radius + x + Add.x + (Radius * FastSin(angle));
            Dots[i].y = h - Radius + y + Add.y + (Radius * FastCos(angle));

            //calc offset
            if (i == 4) { Add.y = -h + (Radius * 2.f); }
            else if (i == 10) { Add.x = w - (Radius * 2.f); }
            else if (i == 16) Add.y = 0.f;
            else if (i == 22) Add.x = 0.f;
        }
        
        //draw polygon
        PolygonInternal(Dots, 24, Color);
    }
};
