#ifndef ENGINE_RENDERING_ENUMS
#define ENGINE_RENDERING_ENUMS

enum {
    BlendMode_NORMAL = 0,
    BlendMode_ADD = 1,
    BlendMode_MAX = 2,
    BlendMode_SUBTRACT = 3,
    BlendMode_MATCH_EQUAL = 4,
    BlendMode_MATCH_NOT_EQUAL = 5,
};

enum {
    BlendFactor_ZERO = 0,
    BlendFactor_ONE = 1,
    BlendFactor_SRC_COLOR = 2,
    BlendFactor_INV_SRC_COLOR = 3,
    BlendFactor_SRC_ALPHA = 4,
    BlendFactor_INV_SRC_ALPHA = 5,
    BlendFactor_DST_COLOR = 6,
    BlendFactor_INV_DST_COLOR = 7,
    BlendFactor_DST_ALPHA = 8,
    BlendFactor_INV_DST_ALPHA = 9,
};

enum {
    DrawBehavior_HorizontalParallax = 0,
    DrawBehavior_VerticalParallax = 1,
    DrawBehavior_CustomTileScanLines = 2,
    DrawBehavior_PGZ1_BG = 3,
};

#define TILE_FLIPX_MASK 0x80000000U
#define TILE_FLIPY_MASK 0x40000000U
// #define TILE_DIAGO_MASK 0x20000000U
#define TILE_COLLA_MASK 0x30000000U
#define TILE_COLLB_MASK 0x0C000000U
#define TILE_COLLC_MASK 0x03000000U
#define TILE_IDENT_MASK 0x00FFFFFFU

struct TileScanLine {
    int SrcX;
    int SrcY;
    int DeltaX;
    int DeltaY;
};
struct Viewport {
    float X;
    float Y;
    float Width;
    float Height;
};
struct ClipArea {
    bool Enabled;
    float X;
    float Y;
    float Width;
    float Height;
};
struct Point {
    float X;
    float Y;
    float Z;
};

struct GraphicsFunctions {
    void     (*Init)();
	Uint32   (*GetWindowFlags)();
	void     (*SetGraphicsFunctions)();
	void     (*Dispose)();

	Texture* (*CreateTexture)(Uint32 format, Uint32 access, Uint32 width, Uint32 height);
	Texture* (*CreateTextureFromPixels)(Uint32 width, Uint32 height, void* pixels, int pitch);
	Texture* (*CreateTextureFromSurface)(SDL_Surface* surface);
	int      (*LockTexture)(Texture* texture, void** pixels, int* pitch);
	int      (*UpdateTexture)(Texture* texture, SDL_Rect* src, void* pixels, int pitch);
	int      (*UpdateYUVTexture)(Texture* texture, SDL_Rect* src, void* pixelsY, int pitchY, void* pixelsU, int pitchU, void* pixelsV, int pitchV);
	void     (*UnlockTexture)(Texture* texture);
	void     (*DisposeTexture)(Texture* texture);

	void     (*UseShader)(void* shader);
    void     (*SetTextureInterpolation)(bool interpolate);
	void     (*SetUniformTexture)(Texture* texture, int uniform_index, int slot);
	void     (*SetUniformF)(int location, int count, float* values);
	void     (*SetUniformI)(int location, int count, int* values);

	void     (*UpdateViewport)();
	void     (*UpdateClipRect)();
	void     (*UpdateOrtho)(float left, float top, float right, float bottom);
	void     (*UpdatePerspective)(float fovy, float aspect, float near, float far);
	void     (*UpdateProjectionMatrix)();

	void     (*Clear)();
	void     (*Present)();
	void     (*SetRenderTarget)(Texture* texture);
	void     (*UpdateWindowSize)(int width, int height);

	void     (*SetBlendColor)(float r, float g, float b, float a);
	void     (*SetBlendMode)(int srcC, int dstC, int srcA, int dstA);
	void     (*SetLineWidth)(float n);

	void     (*StrokeLine)(float x1, float y1, float x2, float y2);
	void     (*StrokeCircle)(float x, float y, float rad);
	void     (*StrokeEllipse)(float x, float y, float w, float h);
	void     (*StrokeRectangle)(float x, float y, float w, float h);
	void     (*FillCircle)(float x, float y, float rad);
	void     (*FillEllipse)(float x, float y, float w, float h);
	void     (*FillTriangle)(float x1, float y1, float x2, float y2, float x3, float y3);
	void     (*FillRectangle)(float x, float y, float w, float h);

	void     (*DrawTexture)(Texture* texture, float sx, float sy, float sw, float sh, float x, float y, float w, float h);
	void     (*DrawSprite)(ISprite* sprite, int animation, int frame, int x, int y, bool flipX, bool flipY, float scaleW, float scaleH, float rotation);
    void     (*DrawSpritePart)(ISprite* sprite, int animation, int frame, int sx, int sy, int sw, int sh, int x, int y, bool flipX, bool flipY, float scaleW, float scaleH, float rotation);

    void     (*MakeFrameBufferID)(ISprite* sprite, AnimFrame* frame);
};

#endif /* ENGINE_RENDERING_ENUMS */
