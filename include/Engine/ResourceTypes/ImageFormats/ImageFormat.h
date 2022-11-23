#ifndef ENGINE_RESOURCETYPES_IMAGEFORMATS_IMAGEFORMAT_H
#define ENGINE_RESOURCETYPES_IMAGEFORMATS_IMAGEFORMAT_H

#define PUBLIC
#define PRIVATE
#define PROTECTED
#define STATIC
#define VIRTUAL
#define EXPOSED


#include <Engine/Includes/Standard.h>

class ImageFormat {
public:
    Uint32* Colors = NULL;
    Uint32* Data = NULL;
    Uint32  Width = 0;
    Uint32  Height = 0;
    Uint32  TransparentColorIndex = 0;
    bool    Paletted = false;

    virtual bool Save(const char* filename);
    virtual      ~ImageFormat();
};

#endif /* ENGINE_RESOURCETYPES_IMAGEFORMATS_IMAGEFORMAT_H */
