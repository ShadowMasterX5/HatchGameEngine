#ifndef ENGINE_RENDERING_TEXTURE_H
#define ENGINE_RENDERING_TEXTURE_H

#define PUBLIC
#define PRIVATE
#define PROTECTED
#define STATIC
#define VIRTUAL
#define EXPOSED

class Texture;
class Texture;
class Texture;

#include <Engine/Includes/Standard.h>

class Texture {
public:
    Uint32   Format;
    Uint32   Access;
    Uint32   Width;
    Uint32   Height;
    void*    Pixels;
    int      Pitch;
    Uint32   ID;
    void*    DriverData;
    Texture* Prev;
    Texture* Next;
    bool     Paletted;

    static Texture* New(Uint32 format, Uint32 access, Uint32 width, Uint32 height);
};

#endif /* ENGINE_RENDERING_TEXTURE_H */
