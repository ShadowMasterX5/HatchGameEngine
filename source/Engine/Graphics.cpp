#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Includes/StandardSDL2.h>

#include <Engine/Application.h>
#include <Engine/Math/Matrix4x4.h>
#include <Engine/ResourceTypes/ISprite.h>
#include <Engine/ResourceTypes/IModel.h>
#include <Engine/Scene/SceneLayer.h>
#include <Engine/Scene/View.h>
#include <Engine/Includes/HashMap.h>
#include <Engine/Rendering/Enums.h>

need_t ISprite;
need_t IModel;

class Graphics {
public:
	static HashMap<Texture*>* TextureMap;
	static HashMap<Texture*>* SpriteSheetTextureMap;
	static bool   		      VsyncEnabled;
	static int 				  MultisamplingEnabled;
	static int 				  FontDPI;
	static bool   		      SupportsBatching;
	static bool   		      TextureBlend;
	static bool   		      TextureInterpolate;
	static Uint32             PreferredPixelFormat;

	static Uint32 		      MaxTextureWidth;
	static Uint32 		      MaxTextureHeight;
	static Texture*           TextureHead;

	static stack<Matrix4x4*>  ProjectionMatrixBackups;
	static stack<Matrix4x4*>  ModelViewMatrix;

	static Viewport           CurrentViewport;
	static Viewport           BackupViewport;
	static ClipArea           CurrentClip;
	static ClipArea           BackupClip;

	static float              BlendColors[4];

	static Texture*           CurrentRenderTarget;

	static void*              CurrentShader;
    static bool               SmoothFill;
    static bool               SmoothStroke;

	// Rendering functions
	static GraphicsFunctions  Internal;
	static GraphicsFunctions* GfxFunctions;
	static const char*        Renderer;
    static float              PixelOffset;
	static bool 			  NoInternalTextures;
	static int   			  BlendMode;
	static bool 			  UsePalettes;
};
#endif

#include <Engine/Graphics.h>

#include <Engine/Diagnostics/Log.h>
#include <Engine/Diagnostics/Memory.h>
#include <Engine/Math/Math.h>

#include <Engine/Rendering/Software/SoftwareRenderer.h>
#ifdef USING_OPENGL
	#include <Engine/Rendering/GL/GLRenderer.h>
#endif
#ifdef USING_DIRECT3D
	#include <Engine/Rendering/D3D/D3DRenderer.h>
#endif
#include <Engine/Rendering/SDL2/SDL2Renderer.h>

#include <Engine/Bytecode/BytecodeObjectManager.h>

HashMap<Texture*>* Graphics::TextureMap = NULL;
HashMap<Texture*>* Graphics::SpriteSheetTextureMap = NULL;
bool   		  	   Graphics::VsyncEnabled = true;
int   		  	   Graphics::MultisamplingEnabled = 0;
int 			   Graphics::FontDPI = 1;
bool   		  	   Graphics::SupportsBatching = false;
bool               Graphics::TextureBlend = false;
bool               Graphics::TextureInterpolate = false;
Uint32             Graphics::PreferredPixelFormat = SDL_PIXELFORMAT_ARGB8888;
Uint32 		       Graphics::MaxTextureWidth = 1;
Uint32 		       Graphics::MaxTextureHeight = 1;
Texture*           Graphics::TextureHead = NULL;

stack<Matrix4x4*>  Graphics::ProjectionMatrixBackups;
stack<Matrix4x4*>  Graphics::ModelViewMatrix;

Viewport           Graphics::CurrentViewport;
Viewport           Graphics::BackupViewport;
ClipArea           Graphics::CurrentClip;
ClipArea           Graphics::BackupClip;

float              Graphics::BlendColors[4];

Texture*           Graphics::CurrentRenderTarget = NULL;

void*              Graphics::CurrentShader = NULL;
bool               Graphics::SmoothFill = false;
bool               Graphics::SmoothStroke = false;

GraphicsFunctions  Graphics::Internal;
GraphicsFunctions* Graphics::GfxFunctions = &Graphics::Internal;
const char*        Graphics::Renderer = "default";
float              Graphics::PixelOffset = 0.0f;
bool               Graphics::NoInternalTextures = false;
int   			   Graphics::BlendMode = 0;
bool               Graphics::UsePalettes = false;

PUBLIC STATIC void     Graphics::Init() {
	Graphics::TextureMap = new HashMap<Texture*>(NULL, 32);
    Graphics::SpriteSheetTextureMap = new HashMap<Texture*>(NULL, 32);

    Graphics::ModelViewMatrix.push(Matrix4x4::Create());

	Graphics::CurrentClip.Enabled = false;
	Graphics::BackupClip.Enabled = false;

	Graphics::BlendColors[0] =
	Graphics::BlendColors[1] =
	Graphics::BlendColors[2] =
	Graphics::BlendColors[3] = 1.0;

	int w, h;
	SDL_GetWindowSize(Application::Window, &w, &h);

	Viewport* vp = &Graphics::CurrentViewport;
	vp->X =
	vp->Y = 0.0f;
	vp->Width = w;
	vp->Height = h;
	Graphics::BackupViewport = Graphics::CurrentViewport;

	Graphics::GfxFunctions->Init();

    Log::Print(Log::LOG_VERBOSE, "CPU Core Count: %d", SDL_GetCPUCount());
	Log::Print(Log::LOG_INFO, "System Memory: %d MB", SDL_GetSystemRAM());
    Log::Print(Log::LOG_VERBOSE, "Window Size: %d x %d", w, h);
    Log::Print(Log::LOG_INFO, "Window Pixel Format: %s", SDL_GetPixelFormatName(SDL_GetWindowPixelFormat(Application::Window)));
    Log::Print(Log::LOG_INFO, "VSync: %s", Graphics::VsyncEnabled ? "true" : "false");
    if (Graphics::MultisamplingEnabled)
        Log::Print(Log::LOG_VERBOSE, "Multisample: %d", Graphics::MultisamplingEnabled);
    else
        Log::Print(Log::LOG_VERBOSE, "Multisample: false");
    Log::Print(Log::LOG_VERBOSE, "Max Texture Size: %d x %d", Graphics::MaxTextureWidth, Graphics::MaxTextureHeight);
}
PUBLIC STATIC void     Graphics::ChooseBackend() {
	char renderer[64];

	SoftwareRenderer::SetGraphicsFunctions();

	// Set renderers
	Graphics::Renderer = NULL;
	if (Application::Settings->GetString("dev", "renderer", renderer)) {
		#ifdef USING_OPENGL
			if (!strcmp(renderer, "opengl")) {
	            Graphics::Renderer = "opengl";
				GLRenderer::SetGraphicsFunctions();
				return;
			}
		#endif
		#ifdef USING_DIRECT3D
			if (!strcmp(renderer, "direct3d")) {
                Graphics::Renderer = "direct3d";
				D3DRenderer::SetGraphicsFunctions();
				return;
			}
		#endif
		if (!Graphics::Renderer) {
			Log::Print(Log::LOG_WARN, "Could not find renderer \"%s\" on this platform!", renderer);
		}
	}

	// Compiler-directed renderer
	#ifdef USING_DIRECT3D
		if (!Graphics::Renderer) {
			Graphics::Renderer = "direct3d";
			D3DRenderer::SetGraphicsFunctions();
		}
	#endif
	#ifdef USING_OPENGL
		if (!Graphics::Renderer) {
			Graphics::Renderer = "opengl";
			GLRenderer::SetGraphicsFunctions();
		}
	#endif
	#ifdef USING_METAL
		if (!Graphics::Renderer) {
			Graphics::Renderer = "metal";
			MetalRenderer::SetGraphicsFunctions();
		}
	#endif

	if (!Graphics::Renderer) {
	    Graphics::Renderer = "sdl2";
	    SDL2Renderer::SetGraphicsFunctions();
	}
}
PUBLIC STATIC Uint32   Graphics::GetWindowFlags() {
    return Graphics::GfxFunctions->GetWindowFlags();
}
PUBLIC STATIC void     Graphics::Dispose() {
	for (Texture* texture = Graphics::TextureHead, *next; texture != NULL; texture = next) {
		next = texture->Next;
        Graphics::DisposeTexture(texture);
    }
	Graphics::TextureHead = NULL;

    Graphics::SpriteSheetTextureMap->Clear();

    Graphics::GfxFunctions->Dispose();

	delete Graphics::TextureMap;
    delete Graphics::SpriteSheetTextureMap;
    while (Graphics::ModelViewMatrix.size()) {
        delete Graphics::ModelViewMatrix.top();
        Graphics::ModelViewMatrix.pop();
    }
}

PUBLIC STATIC Point    Graphics::ProjectToScreen(float x, float y, float z) {
	Point p;
	float vec4[4];
	Matrix4x4* matrix;

	// matrix = Matrix4x4::Create();
	matrix = Graphics::ModelViewMatrix.top();
	// Matrix4x4::Multiply(matrix, Graphics::ProjectionMatrix, Graphics::ModelViewMatrix.top());

	vec4[0] = x; vec4[1] = y; vec4[2] = z; vec4[3] = 1.0f;
	Matrix4x4::Multiply(matrix, &vec4[0]);
	// Matrix4x4::Multiply(Graphics::ProjectionMatrix, &vec4[0]);

	p.X = (vec4[0] + 1.0f) / 2.0f * Graphics::CurrentViewport.Width;
	p.Y = (vec4[1] - 1.0f) / -2.0f * Graphics::CurrentViewport.Height;
	// p.X = vec4[0];
	// p.Y = vec4[1];
	p.Z = vec4[2];
	return p;
}

PUBLIC STATIC Texture* Graphics::CreateTexture(Uint32 format, Uint32 access, Uint32 width, Uint32 height) {
    Texture* texture;
    if (Graphics::GfxFunctions == &SoftwareRenderer::BackendFunctions ||
		Graphics::NoInternalTextures) {
        texture = Texture::New(format, access, width, height);
        if (!texture)
            return NULL;
    }
    else {
        texture = Graphics::GfxFunctions->CreateTexture(format, access, width, height);
    	if (!texture)
    		return NULL;
    }

	texture->Next = Graphics::TextureHead;
	if (Graphics::TextureHead)
		Graphics::TextureHead->Prev = texture;

	Graphics::TextureHead = texture;

	return texture;
}
PUBLIC STATIC Texture* Graphics::CreateTextureFromPixels(Uint32 width, Uint32 height, void* pixels, int pitch) {
    Texture* texture = Graphics::CreateTexture(SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
	if (texture)
		Graphics::UpdateTexture(texture, NULL, pixels, pitch);

	return texture;
}
PUBLIC STATIC Texture* Graphics::CreateTextureFromSurface(SDL_Surface* surface) {
	Texture* texture = Graphics::CreateTexture(surface->format->format, SDL_TEXTUREACCESS_STATIC, surface->w, surface->h);
	if (texture)
		Graphics::GfxFunctions->UpdateTexture(texture, NULL, surface->pixels, surface->pitch);
	return texture;
}
PUBLIC STATIC int      Graphics::LockTexture(Texture* texture, void** pixels, int* pitch) {
	if (Graphics::GfxFunctions == &SoftwareRenderer::BackendFunctions ||
		Graphics::NoInternalTextures)
		return 1;
    return Graphics::GfxFunctions->LockTexture(texture, pixels, pitch);
}
PUBLIC STATIC int      Graphics::UpdateTexture(Texture* texture, SDL_Rect* src, void* pixels, int pitch) {
	memcpy(texture->Pixels, pixels, sizeof(Uint32) * texture->Width * texture->Height);
	if (Graphics::GfxFunctions == &SoftwareRenderer::BackendFunctions ||
		Graphics::NoInternalTextures)
		return 1;
    return Graphics::GfxFunctions->UpdateTexture(texture, src, pixels, pitch);
}
PUBLIC STATIC int      Graphics::UpdateYUVTexture(Texture* texture, SDL_Rect* src, Uint8* pixelsY, int pitchY, Uint8* pixelsU, int pitchU, Uint8* pixelsV, int pitchV) {
	if (!Graphics::GfxFunctions->UpdateYUVTexture)
		return 0;
	if (Graphics::GfxFunctions == &SoftwareRenderer::BackendFunctions ||
		Graphics::NoInternalTextures)
		return 1;
    return Graphics::GfxFunctions->UpdateYUVTexture(texture, src, pixelsY, pitchY, pixelsU, pitchU, pixelsV, pitchV);
}
PUBLIC STATIC void     Graphics::UnlockTexture(Texture* texture) {
    Graphics::GfxFunctions->UnlockTexture(texture);
}
PUBLIC STATIC void     Graphics::DisposeTexture(Texture* texture) {
    Graphics::GfxFunctions->DisposeTexture(texture);

	if (texture->Next)
		texture->Next->Prev = texture->Prev;

	if (texture->Prev)
		texture->Prev->Next = texture->Next;
	else
		Graphics::TextureHead = texture->Next;

	if (texture->Pixels)
		Memory::Free(texture->Pixels);

	Memory::Free(texture);
}

PUBLIC STATIC void     Graphics::UseShader(void* shader) {
	Graphics::CurrentShader = shader;
	Graphics::GfxFunctions->UseShader(shader);
}
PUBLIC STATIC void     Graphics::SetTextureInterpolation(bool interpolate) {
    Graphics::TextureInterpolate = interpolate;
}

PUBLIC STATIC void     Graphics::Clear() {
    Graphics::GfxFunctions->Clear();
}
PUBLIC STATIC void     Graphics::Present() {
    Graphics::GfxFunctions->Present();
}

PUBLIC STATIC void     Graphics::SoftwareStart() {
	Graphics::GfxFunctions = &SoftwareRenderer::BackendFunctions;
    for (int i = 0; i < MAX_PALETTE_COUNT; i++)
        SoftwareRenderer::PaletteColors[i][0] &= 0xFFFFFF;
}
PUBLIC STATIC void     Graphics::SoftwareEnd() {
	// Present to current view texture
	// Scene::Views[Scene::ViewCurrent]
	Graphics::GfxFunctions = &Graphics::Internal;
	Graphics::UpdateTexture(Graphics::CurrentRenderTarget, NULL, Graphics::CurrentRenderTarget->Pixels, Graphics::CurrentRenderTarget->Width * 4);
}

PUBLIC STATIC void     Graphics::SetRenderTarget(Texture* texture) {
	if (texture && !Graphics::CurrentRenderTarget) {
		Graphics::BackupViewport = Graphics::CurrentViewport;
		Graphics::BackupClip = Graphics::CurrentClip;
	}

	// if (Graphics::CurrentRenderTarget != texture) {
    // 	Graphics::ProjectionMatrixBackups.push(Matrix4x4::Create());
	// 	Matrix4x4::Copy(Graphics::ProjectionMatrixBackups.top(), Scene::Views[Scene::ViewCurrent].ProjectionMatrix);
	// }
	// else {
	// 	Matrix4x4::Copy(Scene::Views[Scene::ViewCurrent].ProjectionMatrix, Graphics::ProjectionMatrixBackups.top());
	// 	delete Graphics::ProjectionMatrixBackups.top();
	// 	Graphics::ProjectionMatrixBackups.pop();
	// }

	Graphics::CurrentRenderTarget = texture;

    Graphics::GfxFunctions->SetRenderTarget(texture);

	Viewport* vp = &Graphics::CurrentViewport;
	if (texture) {
		vp->X = 0.0;
		vp->Y = 0.0;
		vp->Width = texture->Width;
		vp->Height = texture->Height;
	}
	else {
		Graphics::CurrentViewport = Graphics::BackupViewport;
		Graphics::CurrentClip = Graphics::BackupClip;
	}

    Graphics::GfxFunctions->UpdateViewport();
	Graphics::GfxFunctions->UpdateClipRect();
}
PUBLIC STATIC void     Graphics::UpdateOrtho(float width, float height) {
    Graphics::GfxFunctions->UpdateOrtho(0.0f, 0.0f, width, height);
}
PUBLIC STATIC void     Graphics::UpdateOrthoFlipped(float width, float height) {
    Graphics::GfxFunctions->UpdateOrtho(0.0f, height, width, 0.0f);
}
PUBLIC STATIC void     Graphics::UpdatePerspective(float fovy, float aspect, float nearv, float farv) {
    Graphics::GfxFunctions->UpdatePerspective(fovy, aspect, nearv, farv);
}
PUBLIC STATIC void     Graphics::UpdateProjectionMatrix() {
    Graphics::GfxFunctions->UpdateProjectionMatrix();
}
PUBLIC STATIC void     Graphics::SetViewport(float x, float y, float w, float h) {
	Viewport* vp = &Graphics::CurrentViewport;
	if (x < 0) {
		vp->X = 0.0;
		vp->Y = 0.0;
		if (Graphics::CurrentRenderTarget) {
			vp->Width = Graphics::CurrentRenderTarget->Width;
			vp->Height = Graphics::CurrentRenderTarget->Height;
		}
		else {
			int w, h;
			SDL_GetWindowSize(Application::Window, &w, &h);
			vp->Width = w;
			vp->Height = h;
		}
	}
	else {
		vp->X = x;
		vp->Y = y;
		vp->Width = w;
		vp->Height = h;
	}
    Graphics::GfxFunctions->UpdateViewport();
}
PUBLIC STATIC void     Graphics::ResetViewport() {
	Graphics::SetViewport(-1.0, 0.0, 0.0, 0.0);
}
PUBLIC STATIC void     Graphics::Resize(int width, int height) {
    Graphics::GfxFunctions->UpdateWindowSize(width, height);
}

PUBLIC STATIC void     Graphics::SetClip(int x, int y, int width, int height) {
	Graphics::CurrentClip.Enabled = true;
	Graphics::CurrentClip.X = x;
	Graphics::CurrentClip.Y = y;
	Graphics::CurrentClip.Width = width;
	Graphics::CurrentClip.Height = height;
	Graphics::GfxFunctions->UpdateClipRect();
}
PUBLIC STATIC void     Graphics::ClearClip() {
	Graphics::CurrentClip.Enabled = false;
	Graphics::GfxFunctions->UpdateClipRect();
}

PUBLIC STATIC void     Graphics::Save() {
	Matrix4x4* top = ModelViewMatrix.top();
    Matrix4x4* push = Matrix4x4::Create();
    Matrix4x4::Copy(push, top);

    if (ModelViewMatrix.size() == 256) {
		Log::Print(Log::LOG_ERROR, "Draw.Save stack too big.");
		exit(-1);
	}

    ModelViewMatrix.push(push);
}
PUBLIC STATIC void     Graphics::Translate(float x, float y, float z) {
	Matrix4x4::Translate(ModelViewMatrix.top(), ModelViewMatrix.top(), x, y, z);
}
PUBLIC STATIC void     Graphics::Rotate(float x, float y, float z) {
	Matrix4x4::Rotate(ModelViewMatrix.top(), ModelViewMatrix.top(), x, 1.0, 0.0, 0.0);
    Matrix4x4::Rotate(ModelViewMatrix.top(), ModelViewMatrix.top(), y, 0.0, 1.0, 0.0);
    Matrix4x4::Rotate(ModelViewMatrix.top(), ModelViewMatrix.top(), z, 0.0, 0.0, 1.0);
}
PUBLIC STATIC void     Graphics::Scale(float x, float y, float z) {
    Matrix4x4::Scale(ModelViewMatrix.top(), ModelViewMatrix.top(), x, y, z);
}
PUBLIC STATIC void     Graphics::Restore() {
	if (ModelViewMatrix.size() == 1) return;

    delete ModelViewMatrix.top();
    ModelViewMatrix.pop();
}

PUBLIC STATIC void     Graphics::SetBlendColor(float r, float g, float b, float a) {
	Graphics::BlendColors[0] = r;
	Graphics::BlendColors[1] = g;
	Graphics::BlendColors[2] = b;
	Graphics::BlendColors[3] = a;
    Graphics::GfxFunctions->SetBlendColor(r, g, b, a);
}
PUBLIC STATIC void     Graphics::SetBlendMode(int srcC, int dstC, int srcA, int dstA) {
    Graphics::GfxFunctions->SetBlendMode(srcC, dstC, srcA, dstA);
}
PUBLIC STATIC void     Graphics::SetLineWidth(float n) {
    Graphics::GfxFunctions->SetLineWidth(n);
}

PUBLIC STATIC void     Graphics::StrokeLine(float x1, float y1, float x2, float y2) {
    Graphics::GfxFunctions->StrokeLine(x1, y1, x2, y2);
}
PUBLIC STATIC void     Graphics::StrokeCircle(float x, float y, float rad) {
    Graphics::GfxFunctions->StrokeCircle(x, y, rad);
}
PUBLIC STATIC void     Graphics::StrokeEllipse(float x, float y, float w, float h) {
    Graphics::GfxFunctions->StrokeEllipse(x, y, w, h);
}
PUBLIC STATIC void     Graphics::StrokeTriangle(float x1, float y1, float x2, float y2, float x3, float y3) {
	Graphics::GfxFunctions->StrokeLine(x1, y1, x2, y2);
	Graphics::GfxFunctions->StrokeLine(x2, y2, x3, y3);
	Graphics::GfxFunctions->StrokeLine(x3, y3, x1, y1);
}
PUBLIC STATIC void     Graphics::StrokeRectangle(float x, float y, float w, float h) {
    Graphics::GfxFunctions->StrokeRectangle(x, y, w, h);
}
PUBLIC STATIC void     Graphics::FillCircle(float x, float y, float rad) {
    Graphics::GfxFunctions->FillCircle(x, y, rad);
}
PUBLIC STATIC void     Graphics::FillEllipse(float x, float y, float w, float h) {
    Graphics::GfxFunctions->FillEllipse(x, y, w, h);
}
PUBLIC STATIC void     Graphics::FillTriangle(float x1, float y1, float x2, float y2, float x3, float y3) {
	Graphics::GfxFunctions->FillTriangle(x1, y1, x2, y2, x3, y3);
}
PUBLIC STATIC void     Graphics::FillRectangle(float x, float y, float w, float h) {
    Graphics::GfxFunctions->FillRectangle(x, y, w, h);
}

PUBLIC STATIC void     Graphics::DrawTexture(Texture* texture, float sx, float sy, float sw, float sh, float x, float y, float w, float h) {
    Graphics::GfxFunctions->DrawTexture(texture, sx, sy, sw, sh, x, y, w, h);
}
PUBLIC STATIC void     Graphics::DrawSprite(ISprite* sprite, int animation, int frame, int x, int y, bool flipX, bool flipY, float scaleW, float scaleH, float rotation) {
    Graphics::GfxFunctions->DrawSprite(sprite, animation, frame, x, y, flipX, flipY, scaleW, scaleH, rotation);
}
PUBLIC STATIC void     Graphics::DrawSpritePart(ISprite* sprite, int animation, int frame, int sx, int sy, int sw, int sh, int x, int y, bool flipX, bool flipY, float scaleW, float scaleH, float rotation) {
    Graphics::GfxFunctions->DrawSpritePart(sprite, animation, frame, sx, sy, sw, sh, x, y, flipX, flipY, scaleW, scaleH, rotation);
}

PUBLIC STATIC void     Graphics::DrawTile(int tile, int x, int y, bool flipX, bool flipY) {
	// If possible, uses optimized software-renderer call instead.
	if (Graphics::GfxFunctions == &SoftwareRenderer::BackendFunctions) {
		SoftwareRenderer::DrawTile(tile, x, y, flipX, flipY);
		return;
	}

	TileSpriteInfo info = Scene::TileSpriteInfos[tile];
    Graphics::GfxFunctions->DrawSprite(info.Sprite, info.AnimationIndex, info.FrameIndex, x, y, flipX, flipY, 1.0f, 1.0f, 0.0f);
}
PUBLIC STATIC void     Graphics::DrawSceneLayer_HorizontalParallax(SceneLayer* layer, View* currentView) {
	int tileSize = Scene::TileSize;
	int tileSizeHalf = tileSize >> 1;
	int tileCellMaxWidth = 3 + (currentView->Width / tileSize);
	int tileCellMaxHeight = 2 + (currentView->Height / tileSize);

	int flipX, flipY, col;
	int TileBaseX, TileBaseY, baseX, baseY, tile, tileOrig, baseXOff, baseYOff;

	TileConfig* baseTileCfg = Scene::ShowTileCollisionFlag == 2 ? Scene::TileCfgB : Scene::TileCfgA;

	if (layer->ScrollInfosSplitIndexes && layer->ScrollInfosSplitIndexesCount > 0) {
		int height, index;
		int ix, iy, sourceTileCellX, sourceTileCellY;

		// Vertical
		if (layer->DrawBehavior == DrawBehavior_VerticalParallax) {
			baseXOff = ((((int)currentView->X + layer->OffsetX) * layer->RelativeY) + Scene::Frame * layer->ConstantY) >> 8;
			TileBaseX = 0;

			for (int split = 0, spl; split < 4096; split++) {
				spl = split;
				while (spl >= layer->ScrollInfosSplitIndexesCount) spl -= layer->ScrollInfosSplitIndexesCount;

				height = (layer->ScrollInfosSplitIndexes[spl] >> 8) & 0xFF;

				sourceTileCellX = (TileBaseX >> 4);
				baseX = (sourceTileCellX << 4) + 8;
				baseX -= baseXOff;

				if (baseX - 8 + height < -tileSize)
					goto SKIP_TILE_ROW_DRAW_ROT90;
				if (baseX - 8 >= currentView->Width + tileSize)
					break;

				index = layer->ScrollInfosSplitIndexes[spl] & 0xFF;

				baseYOff = ((((int)currentView->Y + layer->OffsetY) * layer->ScrollInfos[index].RelativeParallax) + Scene::Frame * layer->ScrollInfos[index].ConstantParallax) >> 8;
				TileBaseY = baseYOff;

				// Loop or cut off sourceTileCellX
				if (layer->Flags & SceneLayer::FLAGS_NO_REPEAT_X) {
					if (sourceTileCellX < 0) goto SKIP_TILE_ROW_DRAW_ROT90;
					if (sourceTileCellX >= layer->Width) goto SKIP_TILE_ROW_DRAW_ROT90;
				}
				// sourceTileCellX = sourceTileCellX & layer->WidthMask;
				while (sourceTileCellX < 0) sourceTileCellX += layer->Width;
				while (sourceTileCellX >= layer->Width) sourceTileCellX -= layer->Width;

				// Draw row of tiles
				iy = (TileBaseY >> 4);
				baseY = tileSizeHalf;
				baseY -= baseYOff & 15; // baseY -= baseYOff % 16;

				// To get the leftmost tile, ix--, and start t = -1
				baseY -= 16;
				iy--;

				// sourceTileCellY = ((sourceTileCellY % layer->Height) + layer->Height) % layer->Height;
				for (int t = 0; t < tileCellMaxHeight; t++) {
					// Loop or cut off sourceTileCellX
					sourceTileCellY = iy;
					if (layer->Flags & SceneLayer::FLAGS_NO_REPEAT_Y) {
						if (sourceTileCellY < 0) goto SKIP_TILE_DRAW_ROT90;
						if (sourceTileCellY >= layer->Height) goto SKIP_TILE_DRAW_ROT90;
					}
					else {
						// sourceTileCellY = ((sourceTileCellY % layer->Height) + layer->Height) % layer->Height;
						while (sourceTileCellY < 0) sourceTileCellY += layer->Height;
						while (sourceTileCellY >= layer->Height) sourceTileCellY -= layer->Height;
					}

					tileOrig = tile = layer->Tiles[sourceTileCellX + (sourceTileCellY << layer->WidthInBits)];

					tile &= TILE_IDENT_MASK;
					// "li == 0" should ideally be "layer->DrawGroup == 0", but in some games multiple layers will use DrawGroup == 0, which would look bad & lag
					if (tile != Scene::EmptyTile) { // || li == 0
						flipX = (tileOrig & TILE_FLIPX_MASK);
						flipY = (tileOrig & TILE_FLIPY_MASK);

						int partY = TileBaseX & 0xF;
						if (flipX) partY = tileSize - height - partY;

						TileSpriteInfo info = Scene::TileSpriteInfos[tile];
						Graphics::DrawSpritePart(info.Sprite, info.AnimationIndex, info.FrameIndex, partY, 0, height, tileSize, baseX, baseY, flipX, flipY, 1.0f, 1.0f, 0.0f);

						if (Scene::ShowTileCollisionFlag && Scene::TileCfgA && layer->ScrollInfoCount <= 1) {
							col = 0;
							if (Scene::ShowTileCollisionFlag == 1)
								col = (tileOrig & TILE_COLLA_MASK) >> 28;
							else if (Scene::ShowTileCollisionFlag == 2)
								col = (tileOrig & TILE_COLLB_MASK) >> 26;

							switch (col) {
								case 1:
									Graphics::SetBlendColor(1.0, 1.0, 0.0, 1.0);
									break;
								case 2:
									Graphics::SetBlendColor(1.0, 0.0, 0.0, 1.0);
									break;
								case 3:
									Graphics::SetBlendColor(1.0, 1.0, 1.0, 1.0);
									break;
							}

							int xorFlipX = 0;
							if (flipX)
								xorFlipX = tileSize - 1;

							if (baseTileCfg[tile].IsCeiling ^ flipY) {
								for (int checkX = 0, realCheckX = 0; checkX < tileSize; checkX++) {
									realCheckX = checkX ^ xorFlipX;
									if (baseTileCfg[tile].CollisionTop[realCheckX] < 0xF0) continue;

									Uint8 colH = baseTileCfg[tile].CollisionTop[realCheckX];
									Graphics::FillRectangle(baseX - 8 + checkX, baseY - 8, 1, tileSize - colH);
								}
							}
							else {
								for (int checkX = 0, realCheckX = 0; checkX < tileSize; checkX++) {
									realCheckX = checkX ^ xorFlipX;
									if (baseTileCfg[tile].CollisionTop[realCheckX] < 0xF0) continue;

									Uint8 colH = baseTileCfg[tile].CollisionBottom[realCheckX];
									Graphics::FillRectangle(baseX - 8 + checkX, baseY - 8 + colH, 1, tileSize - colH);
								}
							}
						}
					}

					SKIP_TILE_DRAW_ROT90:
					iy++;
					baseY += tileSize;
				}

				SKIP_TILE_ROW_DRAW_ROT90:
				TileBaseX += height;
			}
		}
		// Horizontal
		else {
			// baseYOff = (int)std::floor(
			//     (currentView->Y + layer->OffsetY) * layer->RelativeY / 256.f +
			//     Scene::Frame * layer->ConstantY / 256.f);
			baseYOff = ((((int)currentView->Y + layer->OffsetY) * layer->RelativeY) + Scene::Frame * layer->ConstantY) >> 8;
			TileBaseY = 0;

			for (int split = 0, spl; split < 4096; split++) {
				spl = split;
				while (spl >= layer->ScrollInfosSplitIndexesCount) spl -= layer->ScrollInfosSplitIndexesCount;

				height = (layer->ScrollInfosSplitIndexes[spl] >> 8) & 0xFF;

				sourceTileCellY = (TileBaseY >> 4);
				baseY = (sourceTileCellY << 4) + 8;
				baseY -= baseYOff;

				if (baseY - 8 + height < -tileSize)
					goto SKIP_TILE_ROW_DRAW;
				if (baseY - 8 >= currentView->Height + tileSize)
					break;

				index = layer->ScrollInfosSplitIndexes[spl] & 0xFF;
				// baseXOff = (int)std::floor(
				//     (currentView->X + layer->OffsetX) * layer->ScrollInfos[index].RelativeParallax / 256.f +
				//     Scene::Frame * layer->ScrollInfos[index].ConstantParallax / 256.f);
				baseXOff = ((((int)currentView->X + layer->OffsetX) * layer->ScrollInfos[index].RelativeParallax) + Scene::Frame * layer->ScrollInfos[index].ConstantParallax) >> 8;
				TileBaseX = baseXOff;

				// Loop or cut off sourceTileCellY
				if (layer->Flags & SceneLayer::FLAGS_NO_REPEAT_Y) {
					if (sourceTileCellY < 0) goto SKIP_TILE_ROW_DRAW;
					if (sourceTileCellY >= layer->Height) goto SKIP_TILE_ROW_DRAW;
				}
				// sourceTileCellY = sourceTileCellY & layer->HeightMask;
				while (sourceTileCellY < 0) sourceTileCellY += layer->Height;
				while (sourceTileCellY >= layer->Height) sourceTileCellY -= layer->Height;

				// Draw row of tiles
				ix = (TileBaseX >> 4);
				baseX = tileSizeHalf;
				baseX -= baseXOff & 15; // baseX -= baseXOff % 16;

				// To get the leftmost tile, ix--, and start t = -1
				baseX -= 16;
				ix--;

				// sourceTileCellX = ((sourceTileCellX % layer->Width) + layer->Width) % layer->Width;
				for (int t = 0; t < tileCellMaxWidth; t++) {
					// Loop or cut off sourceTileCellX
					sourceTileCellX = ix;
					if (layer->Flags & SceneLayer::FLAGS_NO_REPEAT_X) {
						if (sourceTileCellX < 0) goto SKIP_TILE_DRAW;
						if (sourceTileCellX >= layer->Width) goto SKIP_TILE_DRAW;
					}
					else {
						// sourceTileCellX = ((sourceTileCellX % layer->Width) + layer->Width) % layer->Width;
						while (sourceTileCellX < 0) sourceTileCellX += layer->Width;
						while (sourceTileCellX >= layer->Width) sourceTileCellX -= layer->Width;
					}

					tileOrig = tile = layer->Tiles[sourceTileCellX + (sourceTileCellY << layer->WidthInBits)];

					tile &= TILE_IDENT_MASK;
					// "li == 0" should ideally be "layer->DrawGroup == 0", but in some games multiple layers will use DrawGroup == 0, which would look bad & lag
					if (tile != Scene::EmptyTile) { // || li == 0
						flipX = !!(tileOrig & TILE_FLIPX_MASK);
						flipY = !!(tileOrig & TILE_FLIPY_MASK);

						int partY = TileBaseY & 0xF;
						if (flipY) partY = tileSize - height - partY;

						TileSpriteInfo info = Scene::TileSpriteInfos[tile];
						Graphics::DrawSpritePart(info.Sprite, info.AnimationIndex, info.FrameIndex, 0, partY, tileSize, height, baseX, baseY, flipX, flipY, 1.0f, 1.0f, 0.0f);

						if (Scene::ShowTileCollisionFlag && Scene::TileCfgA && layer->ScrollInfoCount <= 1) {
							col = 0;
							if (Scene::ShowTileCollisionFlag == 1)
								col = (tileOrig & TILE_COLLA_MASK) >> 28;
							else if (Scene::ShowTileCollisionFlag == 2)
								col = (tileOrig & TILE_COLLB_MASK) >> 26;

							switch (col) {
								case 1:
									Graphics::SetBlendColor(1.0, 1.0, 0.0, 1.0);
									break;
								case 2:
									Graphics::SetBlendColor(1.0, 0.0, 0.0, 1.0);
									break;
								case 3:
									Graphics::SetBlendColor(1.0, 1.0, 1.0, 1.0);
									break;
							}

							int xorFlipX = 0;
							if (flipX)
								xorFlipX = tileSize - 1;

							if (baseTileCfg[tile].IsCeiling ^ flipY) {
								for (int checkX = 0, realCheckX = 0; checkX < tileSize; checkX++) {
									realCheckX = checkX ^ xorFlipX;
									if (baseTileCfg[tile].CollisionTop[realCheckX] < 0xF0) continue;

									Uint8 colH = baseTileCfg[tile].CollisionTop[realCheckX];
									Graphics::FillRectangle(baseX - 8 + checkX, baseY - 8, 1, tileSize - colH);
								}
							}
							else {
								for (int checkX = 0, realCheckX = 0; checkX < tileSize; checkX++) {
									realCheckX = checkX ^ xorFlipX;
									if (baseTileCfg[tile].CollisionBottom[realCheckX] < 0xF0) continue;

									Uint8 colH = baseTileCfg[tile].CollisionBottom[realCheckX];
									Graphics::FillRectangle(baseX - 8 + checkX, baseY - 8 + colH, 1, tileSize - colH);
								}
							}
						}
					}

					SKIP_TILE_DRAW:
					ix++;
					baseX += tileSize;
				}

				SKIP_TILE_ROW_DRAW:
				TileBaseY += height;
			}
		}
	}
}
PUBLIC STATIC void     Graphics::DrawSceneLayer_VerticalParallax(SceneLayer* layer, View* currentView) {

}
PUBLIC STATIC void     Graphics::DrawSceneLayer(SceneLayer* layer, View* currentView) {
	// If possible, uses optimized software-renderer call instead.
	if (Graphics::GfxFunctions == &SoftwareRenderer::BackendFunctions) {
		SoftwareRenderer::DrawSceneLayer(layer, currentView);
		return;
	}

    switch (layer->DrawBehavior) {
		default:
		case DrawBehavior_HorizontalParallax:
		case DrawBehavior_CustomTileScanLines:
		case DrawBehavior_PGZ1_BG:
			Graphics::DrawSceneLayer_HorizontalParallax(layer, currentView);
			break;
		// case DrawBehavior_VerticalParallax:
		// 	Graphics::DrawSceneLayer_VerticalParallax(layer, currentView);
			break;
	}
}

PUBLIC STATIC void     Graphics::MakeFrameBufferID(ISprite* sprite, AnimFrame* frame) {
    if (Graphics::GfxFunctions->MakeFrameBufferID)
        Graphics::GfxFunctions->MakeFrameBufferID(sprite, frame);
}

PUBLIC STATIC bool     Graphics::SpriteRangeCheck(ISprite* sprite, int animation, int frame) {
	#ifdef DEBUG
	if (!sprite) return true;
	if (animation < 0 || animation >= (int)sprite->Animations.size()) {
		BytecodeObjectManager::Threads[0].ThrowRuntimeError(false, "Animation %d does not exist in sprite %s!", animation, sprite->Filename);
		return true;
	}
	if (frame < 0 || frame >= (int)sprite->Animations[animation].Frames.size()) {
		BytecodeObjectManager::Threads[0].ThrowRuntimeError(false, "Frame %d in animation \"%s\" does not exist in sprite %s!", frame, sprite->Animations[animation].Name, sprite->Filename);
		return true;
	}
	#endif
	return false;
}
