#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Rendering/Texture.h>

class Image {
public:
    char              Filename[256];
    Texture*          TexturePtr = NULL;
};
#endif

#include <Engine/ResourceTypes/Image.h>

#include <Engine/Application.h>
#include <Engine/Graphics.h>

#include <Engine/ResourceTypes/ImageFormats/GIF.h>
#include <Engine/ResourceTypes/ImageFormats/JPEG.h>
#include <Engine/ResourceTypes/ImageFormats/PNG.h>

#include <Engine/Diagnostics/Log.h>
#include <Engine/Diagnostics/Clock.h>
#include <Engine/Diagnostics/Memory.h>
#include <Engine/IO/FileStream.h>
#include <Engine/IO/ResourceStream.h>
#include <Engine/Utilities/StringUtils.h>

PUBLIC Image::Image(const char* filename) {
    strncpy(Filename, filename, 255);
    TexturePtr = Image::LoadTextureFromResource(Filename);
}

PUBLIC void Image::Dispose() {
    if (TexturePtr) {
        Graphics::DisposeTexture(TexturePtr);
        TexturePtr = NULL;
    }
}

PUBLIC Image::~Image() {
    Dispose();
}

PUBLIC STATIC Texture* Image::LoadTextureFromResource(const char* filename) {
    Texture* texture = NULL;
    Uint32*  data = NULL;
    Uint32   width = 0;
    Uint32   height = 0;

    bool paletted = false;
    const char* altered = filename;

    // if ((texture = Graphics::SpriteSheetTextureMap->Get(altered))) {
    //     return texture;
    // }

    Uint32 magic = 0x000000;
    Stream* stream;
    if (strncmp(altered, "file://", 7) == 0)
        stream = FileStream::New(altered + 7, FileStream::READ_ACCESS);
    else
        stream = ResourceStream::New(altered);
    if (stream) {
        magic = stream->ReadUInt32();
        stream->Close();
    }
    else {
        // Log::Print(Log::LOG_ERROR, "Image \"%s\" does not exist!", filename);
        return NULL;
    }


    // 0x474E5089U PNG
    if (magic == 0x474E5089U) {
        Clock::Start();
        PNG* png = PNG::Load(altered);
        if (png) {
            Log::Print(Log::LOG_VERBOSE, "PNG load took %.3f ms (%s)", Clock::End(), altered);
            width = (Uint32)png->Width;
            height = (Uint32)png->Height;

            data = png->Data;
            paletted = png->Paletted;
            Memory::Track(data, "Texture::Data");

            delete png;
        }
        else {
            Log::Print(Log::LOG_ERROR, "PNG could not be loaded!");
            return NULL;
        }
    }
    // 0xE0FFD8FFU JPEG
    else if ((magic & 0xFFFF) == 0xD8FFU) {
        Clock::Start();
        JPEG* jpeg = JPEG::Load(altered);
        if (jpeg) {
            Log::Print(Log::LOG_VERBOSE, "JPEG load took %.3f ms (%s)", Clock::End(), altered);
            width = (Uint32)jpeg->Width;
            height = (Uint32)jpeg->Height;

            data = jpeg->Data;
            Memory::Track(data, "Texture::Data");

            delete jpeg;
        }
        else {
            Log::Print(Log::LOG_ERROR, "JPEG could not be loaded!");
            return NULL;
        }
    }
    else if (StringUtils::StrCaseStr(altered, ".gif")) {
        Clock::Start();
        GIF* gif = GIF::Load(altered);
        if (gif) {
            Log::Print(Log::LOG_VERBOSE, "GIF load took %.3f ms (%s)", Clock::End(), altered);
            width = (Uint32)gif->Width;
            height = (Uint32)gif->Height;

            data = gif->Data;
            paletted = gif->Paletted;
            // Palette = gif->Colors;
            // PaletteAlt = (Uint32*)Memory::TrackedCalloc("Sprite::PaletteAlt", 256, sizeof(Uint32));
            // PaletteCount = 1;

            Memory::Track(data, "Texture::Data");
            // Memory::Track(Palette, "Sprite::Palette");
            // SetTransparentColorIndex(gif->TransparentColorIndex);

            Memory::Free(gif->Colors);

            delete gif;
        }
        else {
            Log::Print(Log::LOG_ERROR, "GIF could not be loaded!");
            return NULL;
        }
    }
    else {
        Log::Print(Log::LOG_ERROR, "Unsupported image format! %s", filename);
        return NULL;
    }

    bool overrideSoftware = false;
    Application::Settings->GetBool("display", "software", &overrideSoftware);
    if (overrideSoftware)
        Graphics::NoInternalTextures = true;

    if (!overrideSoftware && (width > Graphics::MaxTextureWidth || height > Graphics::MaxTextureHeight)) {
		Log::Print(Log::LOG_WARN, "Image file \"%s\" of size %d x %d is larger than maximum size of %d x %d!", altered, width, height, Graphics::MaxTextureWidth, Graphics::MaxTextureHeight);
		// return NULL;
	}

    texture = Graphics::CreateTextureFromPixels(width, height, data, width * sizeof(Uint32));
    texture->Paletted = paletted;

    Graphics::NoInternalTextures = false;

    Memory::Free(data);

    // Graphics::SpriteSheetTextureMap->Put(altered, texture);

    return texture;
}
