
//Copyright (c) 2015 Jason Light
//License: MIT

#include "Renderer.h"

//#include <vector>

#include <Windows.h>

#include "Utility.h"
#include "MemoryManager.h"
#include "AssetManager.h"
#include "Config.h"
#include <intrin.h>

bool ShouldClose = false;

static int MonitorCount;
static Rect Monitors[8];


DWORD WINAPI SlaveThreadProc(LPVOID sArgs) {
	SlaveArgs* Args = (SlaveArgs*) sArgs;
	
	OutputDebugStringA("Spawning renderer helper thread!");

	while (!*Args->ShouldClose) {
		if (Args->Buffer1->shouldDraw) {
			Args->Buffer1->isDrawing = true;
			Args->Buffer1->shouldDraw = false;

			StretchDIBits(*Args->DeviceContext, 0, 0, Args->Config->WindowResX, Args->Config->WindowResY, 0, 0, Args->Buffer1->Width, Args->Buffer1->Height, Args->Buffer1->Memory, &Args->Buffer1->Info, DIB_RGB_COLORS, SRCCOPY);

			for (int i = 0; i < Args->Renderer->Config.RenderResX * Args->Renderer->Config.RenderResY; i++) Args->Buffer1->Memory[i] = rgba(102, 102, 204, 255);
			//Args->Renderer->DrawRectangle(0, 0, Args->Config->RenderResX, Args->Config->RenderResY, rgba(102, 102, 204, 255));

			Args->Buffer1->isDrawing = false;
		}

		if (Args->Buffer2->shouldDraw) {
			Args->Buffer2->isDrawing = true;
			Args->Buffer2->shouldDraw = false;

			StretchDIBits(*Args->DeviceContext, 0, 0, Args->Config->WindowResX, Args->Config->WindowResY, 0, 0, Args->Buffer2->Width, Args->Buffer2->Height, Args->Buffer2->Memory, &Args->Buffer2->Info, DIB_RGB_COLORS, SRCCOPY);
			//Args->Renderer->DrawRectangle(0, 0, Args->Config->RenderResX, Args->Config->RenderResY, rgba(102, 102, 204, 255));

			for (int i = 0; i < Args->Renderer->Config.RenderResX * Args->Renderer->Config.RenderResY; i++) Args->Buffer2->Memory[i] = rgba(102, 102, 204, 255);


			Args->Buffer2->isDrawing = false;
		}
	}

	OutputDebugStringA("Killing renderer helper thread!");

	return 1;
}

//The window callback, this is what processes messages from our windows
LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam) {

	static bool MouseOver = false;

	switch (Message) {
	case WM_CLOSE:
		DestroyWindow(Window);
		ShouldClose = true;
		return 0;
	case WM_KEYDOWN:
		switch (wParam){
		case VK_UP:
			break;
		}

	case WM_KEYUP:
		if (wParam == VK_ESCAPE) {
			DestroyWindow(Window);
			ShouldClose = true;
			return 0;
		}


	default:
		return DefWindowProc(Window, Message, wParam, lParam);
	}
}

//A function to blend two pixels based on the 
inline void Blend(unsigned int* Source, unsigned int* Dest) {
	//Get the source channels
	byte* SA = ((byte*)Source) + 3;
	byte* SB = ((byte*)Source) + 2;
	byte* SG = ((byte*)Source) + 1;
	byte* SR = ((byte*)Source);

	//Get the destination channels
	byte* DB = ((byte*)Dest) + 2;
	byte* DG = ((byte*)Dest) + 1; 
	byte* DR = ((byte*)Dest);

	//Set the destination color based on the source and destination channel
	*DR = *SR + ((*DR * (256 - *SA)) >> 8);
	*DG = *SG + ((*DG * (256 - *SA)) >> 8);
	*DB = *SB + ((*DB * (256 - *SA)) >> 8);
}

bool ResizeSprite(Sprite* Sprite, int W) {
	for (int i = 0; i <= Sprite->NumberOfFrames; i++)
		if (!ResizeSprite(&Sprite->Frames[i], W)) return false;

	Sprite->Width = Sprite->Frames->Width;
	Sprite->Height = Sprite->Frames->Height;

	return true;
}

bool ResizeSprite(_Sprite* Sprite, int W) {
	assert(W > 0, "Tried to resize sprite to zero width");
	assert(Sprite->Width != 0, "Tried to resize invalid sprite");

	return ResizeSprite(Sprite, W, Sprite->Height * (W / Sprite->Width));
}

bool ResizeSprite(Sprite* Sprite, int W, int H) {
	for (int i = 0; i < Sprite->NumberOfFrames; i++)
		if (!ResizeSprite(&Sprite->Frames[i], W, H)) return false;

	Sprite->Width = Sprite->Frames->Width;
	Sprite->Height = Sprite->Frames->Height;

	return true;
}

//A function to resize a sprite using nearest neighbour
bool ResizeSprite(_Sprite* Sprite, int W, int H){
	//Initialize a temporary buffer for the sprite
	unsigned int *TempBuffer = (unsigned int*)VirtualAlloc(0, W * H * 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	
	//Calculate the scaling ratios
	//I shift the values up by 16 bits to effectively create a fixed point decimal number
	//The top 16 bits are the whole part, and the bottom are the fractional part
	//This is cheaper and more precise than floating point numbers 
	//(Also the FPU is likely under heavy load from the physics engine)
	unsigned long Scale_X = (int)((Sprite->Width << 16) / W) + 1;
	unsigned long Scale_Y = (int)((Sprite->Height << 16) / H) + 1;

	//Location of the original pixel
	int PixelX, PixelY;
	
	for (int Y = 0; Y < H; Y++) {
		for (int X = 0; X < W; X++) {
			//Find the pixel at the correct location
			//At this point we shift the numbers back to normal integers
			PixelX = (int)(X * Scale_X) >> 16;
			PixelY = (int)(Y * Scale_Y) >> 16;

			TempBuffer[Y * W + X] = Sprite->Data[(PixelY * Sprite->Width) + PixelX];
		}
	}

	//Delete the old Sprite data
	if (Sprite->Data) VirtualFree(Sprite->Data, 0, MEM_RELEASE);
	
	//Set the sprite data to the new buffer
	Sprite->Data = TempBuffer;

	//Set up the sprites members according to the change
	Sprite->Width = W;
	Sprite->Height = H;
	Sprite->length = W * H * 4;

	return true;
}

bool _Sprite::Load(AssetFile AssetFile, int id){
	Asset BMP = AssetFile.GetAsset(id);
	
	byte* Memory = (byte*)BMP.Memory;
	ImageHeader* Header = (ImageHeader*)Memory;
	
	this->Width = Header->Width;
	this->Height = Header->Height;
	this->length = Header->length;
	this->hasTransparency = Header->HasTransparency != 0;
	
	this->Data = (u32*)VirtualAlloc(0, Header->length, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	memcpy((void*)this->Data, (void*)(Memory + sizeof(ImageHeader)), Header->length);
	
	return true;
}

_Sprite::~_Sprite() {
	if (this->Data) VirtualFree(this->Data, 0, MEM_RELEASE);
}

bool Renderer::OpenWindow(int Width, int Height, char* Title){
	WNDCLASSEX WindClass = {}; //Create a Window Class structure
	
	//Check if the class has been registered aleady	
	if (!GetClassInfoEx(Instance, "JasonWindowClassName", &WindClass)) {
		//Create and fill a Window Class structure
		WindClass.hInstance = Instance;
		WindClass.lpfnWndProc = WindowProc;
		WindClass.cbSize = sizeof(WindClass);
		WindClass.style = CS_HREDRAW | CS_VREDRAW; //Redraw the Window if it is moved or resized horizontally or vertically
		WindClass.lpszClassName = "JasonWindowClassName"; //Unique identifying class name

		if (!RegisterClassEx(&WindClass)) {
			DWORD error = GetLastError();
			DisplayMessage(error);
			return false;
		}
	}

	DWORD WindowStyle = Config.Fullscreen ? (WS_POPUP | WS_VISIBLE) : (WS_OVERLAPPEDWINDOW ^ WS_THICKFRAME ^ WS_MAXIMIZEBOX | WS_VISIBLE);
	
	RECT WindowRect = { 0, 0, Width, Height };
	AdjustWindowRect(&WindowRect, WindowStyle, false);

	this->Window = CreateWindowExA(WS_EX_OVERLAPPEDWINDOW, "JasonWindowClassName", Title, WindowStyle, 0, 0, WindowRect.right, WindowRect.bottom, 0, 0, Instance, 0);
	return true;
}

struct MonitorEnumResult {
	HMONITOR Monitors[8];
	int Primary = 0;
	int Count = 0;
};

BOOL CALLBACK MonitorEnumProc(HMONITOR Monitor, HDC DeviceContext, LPRECT Rect, LPARAM Data) {
	MonitorEnumResult* Monitors = (MonitorEnumResult*)Data;
	Monitors->Monitors[Monitors->Count++] = Monitor;

	MONITORINFO MonitorInfo;
	MonitorInfo.cbSize = sizeof(MONITORINFO);
	GetMonitorInfo(Monitor, &MonitorInfo);

	if (MonitorInfo.dwFlags & MONITORINFOF_PRIMARY) Monitors->Primary = Monitors->Count - 1;
	return true;
}

void Renderer::SetCameraPosition(int X, int Y) {
	this->CameraPos = { X, Y };
}

void Renderer::SetCameraPosition(IVec2 Position) {
	this->CameraPos = Position;
}

bool Renderer::Initialize() {

	Buffer1 = new Win32ScreenBuffer;
	Buffer2 = new Win32ScreenBuffer;

	Buffer = Buffer1;

	SetCameraPosition({ 0, 0 });

	ConfigFile GraphicsConfig("config/Graphics.ini");

	std::string sFullscreen = GraphicsConfig.Get("Fullscreen", "1");
	
	Config.Fullscreen = sFullscreen != "0";

	std::string sRenderResX = GraphicsConfig.Get("RenderResolutionX");
	std::string sRenderResY = GraphicsConfig.Get("RenderResolutionY");

	if (sRenderResX != ""){
		Config.RenderResX = std::atoi(sRenderResX.c_str());
	}
	else {
		Config.RenderResX = Config.Fullscreen ? 0 : 1024;
	}

	if (sRenderResY != ""){
		Config.RenderResY = std::atoi(sRenderResY.c_str());
	}
	else {
		Config.RenderResY = Config.Fullscreen ? 0 : 768;
	}

	if (Config.Fullscreen) {
		MonitorEnumResult Monitors;

		EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&Monitors);

		int Monitor = std::atoi(GraphicsConfig.Get("Monitor", std::to_string(Monitors.Primary)).c_str());

		MONITORINFO MonitorInfo;
		MonitorInfo.cbSize = sizeof(MONITORINFO);
		GetMonitorInfo(Monitors.Monitors[Monitor], &MonitorInfo);

		Config.WindowResY = MonitorInfo.rcMonitor.bottom - MonitorInfo.rcMonitor.top;
		Config.WindowResX = MonitorInfo.rcMonitor.right - MonitorInfo.rcMonitor.left;

		if (Config.RenderResX == 0) Config.RenderResX = Config.WindowResX;
		if (Config.RenderResY == 0) Config.RenderResY = Config.WindowResY;
	}
	else {
		Config.WindowResX = Config.RenderResX;
		Config.WindowResY = Config.RenderResY;
	}

	GraphicsConfig.Set("RenderResolutionX", std::to_string(Config.RenderResX));
	GraphicsConfig.Set("RenderResolutionY", std::to_string(Config.RenderResY));

	Config.BPP = 4;

	Instance = GetModuleHandle(NULL);
	this->OpenWindow(Config.WindowResX, Config.WindowResY, "Title");
	this->DeviceContext = GetWindowDC(this->Window);

	while (ShowCursor(false) >= 0);

	//Create a DIB to render to
	if (Buffer->Memory) {
		VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
	}

	Buffer->Width = Config.RenderResX;
	Buffer->Height = Config.RenderResY;
	Buffer->BytesPerPixel = 4;

	Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
	Buffer->Info.bmiHeader.biWidth = Buffer->Width;
	Buffer->Info.bmiHeader.biHeight = -Buffer->Height;
	Buffer->Info.bmiHeader.biPlanes = 1;
	Buffer->Info.bmiHeader.biBitCount = 32;
	Buffer->Info.bmiHeader.biCompression = BI_RGB;

	if (Buffer2->Memory) {
		VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
	}

	Buffer2->Width = Config.RenderResX;
	Buffer2->Height = Config.RenderResY;
	Buffer2->BytesPerPixel = 4;

	Buffer2->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
	Buffer2->Info.bmiHeader.biWidth = Buffer->Width;
	Buffer2->Info.bmiHeader.biHeight = -Buffer->Height;
	Buffer2->Info.bmiHeader.biPlanes = 1;
	Buffer2->Info.bmiHeader.biBitCount = 32;
	Buffer2->Info.bmiHeader.biCompression = BI_RGB;

	int MemorySize = Buffer->Width * Buffer->Height * Buffer->BytesPerPixel;

//	BM = CreateDIBSection(DeviceContext, &Buffer->Info, DIB_RGB_COLORS, (void**)&Buffer->Memory, NULL, 0);
	Buffer->Memory = (int*)MemoryManager::AllocateMemory(MemorySize);
	Buffer2->Memory = (int*)MemoryManager::AllocateMemory(MemorySize);

	SlaveArguments.Buffer1 = Buffer1;
	SlaveArguments.Buffer2 = Buffer2;
	SlaveArguments.Config = &Config;
	SlaveArguments.DeviceContext = &DeviceContext;
	SlaveArguments.Renderer = this;
	SlaveArguments.ShouldClose = &ShouldClose;

	//CreateThread(NULL, 0, SlaveThreadProc, (void*)&SlaveArguments, 0, 0);

	return 0;
}

bool Renderer::Refresh() {
	//Now update the screen
	//Stretch to the screen
	StretchDIBits(DeviceContext, 0, 0, Config.WindowResX, Config.WindowResY, 0, 0, Buffer->Width, Buffer->Height, Buffer->Memory, &Buffer->Info, DIB_RGB_COLORS, SRCCOPY);
	
	DrawRectangle(0, 0, Config.RenderResX, Config.RenderResY, rgba(102,102,204,255));
	return !ShouldClose;
}

void Renderer::DrawRectangle(int X, int Y, int Width, int Height, unsigned int Color) {

	if (X < 0) {
		Width += X;
		X = 0;
		if (Width <= 0) return;
	}

	if (Y < 0) {
		Height += Y;
		Y = 0;
		if (Height <= 0) return;
	}

	if (X + Width > Buffer->Width) {
		Width = Buffer->Width - X;
		X = Buffer->Width - Width;

		if (Width <= 0) return;
	}

	if (Y + Height > Buffer->Height) {
		Height = Buffer->Height - Y;
		Y = Buffer->Height - Height;

		if (Height <= 0) return;
	}

	for (int y = 0; y < Height; y++) {
		int Down = Buffer->Width * (Y + y);
		for (int x = 0; x < Width; x++) {
			((unsigned int*)Buffer->Memory)[Down + (X + x)] = Color;
		}
	}
}

void Renderer::DrawRectangleBlend(int X, int Y, int Width, int Height, unsigned int Color) {

	if (X < 0) {
		Width += X;
		X = 0;
		if (Width <= 0) return;
	}

	if (Y < 0) {
		Height += Y;
		Y = 0;
		if (Height <= 0) return;
	}

	if (X + Width > Buffer->Width) {
		Width = Buffer->Width - X;
		X = Buffer->Width - Width;

		if (Width <= 0) return;
	}

	if (Y + Height > Buffer->Height) {
		Height = Buffer->Height - Y;
		Y = Buffer->Height - Height;

		if (Height <= 0) return;
	}

	for (int y = 0; y < Height; y++){
		int Down = Buffer->Width * (Y + y);
		for (int x = 0; x < Width; x++) {
			Blend(&Color, (unsigned int*)(Buffer->Memory + Down + (X + x)));
		}
	}
}

void Renderer::DrawRectangleWS(int X, int Y, int Width, int Height, unsigned int Color) {
	X -= CameraPos.X;
	Y -= CameraPos.Y;

	DrawRectangle(X, Y, Width, Height, Color);
}

void Renderer::DrawRectangleBlendWS(int X, int Y, int Width, int Height, unsigned int Color) {

	X -= CameraPos.X;
	Y -= CameraPos.Y;

	DrawRectangleBlend(X, Y, Width, Height, Color);

}

void Renderer::DrawSpriteRectangle(int X, int Y, int Width, int Height, _Sprite* Spr) {
	for (int y = Y; y < Y + Height; y+= Spr->Width){
		int Down = Buffer->Width * (Y + y);
		for (int x = X; x < X + Width; x += Spr->Width) {

			x -= CameraPos.X;
			y -= CameraPos.Y;


			DrawSprite(Spr, 0, 0, Spr->Width, Spr->Height, x, y);
		}
	}
}

void Renderer::DrawSprite(Sprite* Spr, int X, int Y) {
	DrawSprite(Spr, 0, 0, Spr->Width, Spr->Height, X, Y, true);
}

void Renderer::DrawSprite(Sprite* Spr, int SrcX, int SrcY, int Width, int Height, int DstX, int DstY) {
	DrawSprite(Spr, SrcX, SrcY, Width, Height, DstX, DstY, true);
}

void Renderer::DrawSprite(_Sprite* Spr, int X, int Y) {
	DrawSprite(Spr, 0, 0, Spr->Width, Spr->Height, X, Y, true);
}

void Renderer::DrawSprite(_Sprite* Spr, int SrcX, int SrcY, int Width, int Height, int DstX, int DstY){
	DrawSprite(Spr, SrcX, SrcY, Width, Height, DstX, DstY, true);
}

void Renderer::DrawSprite(_Sprite* Spr, int SrcX, int SrcY, int Width, int Height, int DstX, int DstY, bool ShouldBlend){
	//Out of bounds checks
	//If the sprite is drawn partially offscreen, draw a section
	//If it is fully offscreen, don't draw it.

	DstX -= CameraPos.X;
	DstY -= CameraPos.Y;

	DrawSpriteSS(Spr, SrcX, SrcY, Width, Height, DstX, DstY, ShouldBlend);
}

void Renderer::DrawSprite(Sprite * Spr, int SrcX, int SrcY, int Width, int Height, int DstX, int DstY, bool Blend) {
	int Frame;

	if (Spr->isAnimated) {
		Frame = ((GetTickCount() - Spr->CreationTime) / (Spr->Period / Spr->NumberOfFrames)) % Spr->NumberOfFrames;
	}
	else {
		Frame = 0;
	}
	DrawSprite(Spr->Frames + Frame, SrcX, SrcY, Width, Height, DstX, DstY, Blend);
}


void Renderer::DrawSpriteSS(Sprite * Spr, int X, int Y) {
	DrawSpriteSS(Spr, 0, 0, Spr->Width, Spr->Height, X, Y, true);
}

void Renderer::DrawSpriteSS(Sprite * Spr, int SrcX, int SrcY, int Width, int Height, int DstX, int DstY) {
	DrawSpriteSS(Spr, SrcX, SrcY, Width, Height, DstX, DstY, true);
}

void Renderer::DrawSpriteSS(Sprite * Spr, int SrcX, int SrcY, int Width, int Height, int DstX, int DstY, bool Blend) {
	int Frame;

	if (Spr->isAnimated) {
		Frame = ((GetTickCount() - Spr->CreationTime) / (Spr->Period / Spr->NumberOfFrames)) % Spr->NumberOfFrames;
	}
	else {
		Frame = 0;
	}
	DrawSpriteSS(Spr->Frames + Frame, SrcX, SrcY, Width, Height, DstX, DstY, Blend);
}

void Renderer::DrawSpriteSS(_Sprite * Spr, int X, int Y) {
	DrawSpriteSS(Spr, 0, 0, Spr->Width, Spr->Height, X, Y, true);
}

void Renderer::DrawSpriteSS(_Sprite * Spr, int SrcX, int SrcY, int Width, int Height, int DstX, int DstY) {
	DrawSpriteSS(Spr, SrcX, SrcY, Width, Height, DstX, DstY, true);
}

void Renderer::DrawSpriteSS(_Sprite * Spr, int SrcX, int SrcY, int Width, int Height, int DstX, int DstY, bool ShouldBlend) {
	if (DstX < 0) {
		SrcX -= DstX;
		Width += DstX;

		DstX = 0;
		if (Width <= 0) return;
	}

	if (DstY < 0) {
		SrcY -= DstY;
		Height += DstY;

		DstY = 0;
		if (Height <= 0) return;
	}

	if (DstX + Width > Buffer->Width) {
		Width = Buffer->Width - DstX;
		DstX = Buffer->Width - Width;

		if (Width <= 0) return;
	}

	if (DstY + Height > Buffer->Height) {
		Height = Buffer->Height - DstY;
		DstY = Buffer->Height - Height;

		if (Height <= 0) return;
	}

	for (register int y = SrcY; y < (SrcY + Height); y++) {
		if ((ShouldBlend && Spr->hasTransparency) || true) {
			//If the pixel should be drawn with transparency itterate over each pixel
			for (register int x = SrcX; x < (SrcX + Width); x++) {
				unsigned int ARGB = Spr->Data[y * Spr->Width + x];
				unsigned char* SA = ((unsigned char*)&ARGB) + 3;

				if (*SA == 0) { //If the pixel is fully transparent, skip to the next loop itteration as no rendering is needed
					continue;
				}
				else if (*SA == 255) { //If the pixel has no transparency, copy it into the destination
					((unsigned int*)Buffer->Memory)[((y - SrcY) + DstY) * Buffer->Width + ((x - SrcX) + DstX)] = ARGB;
				}
				else { //Otherwise blend it properly
					Blend(&ARGB, &((unsigned int*)Buffer->Memory)[((y - SrcY) + DstY) * Buffer->Width + ((x - SrcX) + DstX)]);
				}
			}
		}
		else {
			//If the sprite has no transparency, or we are drawing without blending enabled, use memcpy to copy entire rows at once for speed
			memcpy((void*)&((unsigned int*)Buffer->Memory)[((y - SrcY) + DstY) * Buffer->Width + DstX], (void*)&(Spr->Data[y * Spr->Width]), Width * 4);
		}
	}
}

bool Sprite::Load(AssetFile Asset, int id)
{
	Frames = MemoryManager::AllocateMemory<_Sprite>(1);

	if (!Frames->Load(Asset, id)) return false;

	Width = Frames->Width;
	Height = Frames->Height;

	CurrentFrame = 0;
	NumberOfFrames = 0;
	isAnimated = false;

	return true;
}

bool Sprite::Load(AssetFile Asset, int start, int amount) {
	Frames = MemoryManager::AllocateMemory<_Sprite>(amount);

	for (int i = 0; i < amount; i++) {
		if (!Frames[i].Load(Asset, start + i)) return false;
		ResizeSprite(Frames + i, 48);
	}

	Width = Frames->Width;
	Height = Frames->Height;

	CurrentFrame = 0;
	NumberOfFrames = amount;
	isAnimated = true;

	CreationTime = GetTickCount();
	return true;
}
