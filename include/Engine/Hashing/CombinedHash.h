#ifndef ENGINE_HASHING_COMBINEDHASH_H
#define ENGINE_HASHING_COMBINEDHASH_H

#define PUBLIC
#define PRIVATE
#define PROTECTED
#define STATIC
#define VIRTUAL
#define EXPOSED


#include <Engine/Includes/Standard.h>

class CombinedHash {
public:
    static Uint32 EncryptString(char* data);
    static Uint32 EncryptString(const char* message);
    static Uint32 EncryptData(const void* message, size_t len);
};

#endif /* ENGINE_HASHING_COMBINEDHASH_H */
