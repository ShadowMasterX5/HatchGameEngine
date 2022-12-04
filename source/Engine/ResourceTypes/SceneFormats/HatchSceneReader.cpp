#if INTERFACE
#include <Engine/IO/ResourceStream.h>
#include <Engine/Scene/SceneLayer.h>
#include <Engine/ResourceTypes/SceneFormats/HatchSceneTypes.h>

class HatchSceneReader {
public:
    static Uint32 Magic;
};
#endif

#include <Engine/ResourceTypes/SceneFormats/HatchSceneReader.h>
#include <Engine/ResourceTypes/SceneFormats/HatchSceneTypes.h>

#include <Engine/IO/MemoryStream.h>
#include <Engine/Bytecode/BytecodeObjectManager.h>
#include <Engine/Bytecode/BytecodeObject.h>
#include <Engine/Bytecode/Compiler.h>
#include <Engine/Diagnostics/Clock.h>
#include <Engine/Diagnostics/Log.h>
#include <Engine/Diagnostics/Memory.h>
#include <Engine/Hashing/CRC32.h>
#include <Engine/Hashing/MD5.h>
#include <Engine/Hashing/CombinedHash.h>
#include <Engine/IO/ResourceStream.h>
#include <Engine/Includes/HashMap.h>
#include <Engine/ResourceTypes/ResourceManager.h>
#include <Engine/Utilities/StringUtils.h>
#include <Engine/Scene/SceneLayer.h>
#include <Engine/Scene.h>

Uint32 HatchSceneReader::Magic = 0x4E435348; // HSCN

#define HSCN_EMPTY_TILE 0xFFFFFFFFU

#define HSCN_COLLA_MASK 0x0000C000U
#define HSCN_COLLB_MASK 0x00030000U
#define HSCN_FLIPX_MASK 0x00001000U
#define HSCN_FLIPY_MASK 0x00002000U
#define HSCN_FXYID_MASK 0x00003FFFU // Max. 4096 tiles

#define TILE_FLIPX_MASK 0x80000000U
#define TILE_FLIPY_MASK 0x40000000U
#define TILE_IDENT_MASK 0x00FFFFFFU // Max. 16777216 tiles

PUBLIC STATIC bool HatchSceneReader::Read(const char* filename, const char* parentFolder) {
    Stream* r = ResourceStream::New(filename);
    if (!r) {
        Log::Print(Log::LOG_ERROR, "Couldn't open file '%s'!", filename);
        return false;
    }

    return HatchSceneReader::Read(r, parentFolder);
}

PUBLIC STATIC bool HatchSceneReader::Read(Stream* r, const char* parentFolder) {
    // Init scene
    Scene::TileSize = 16;
    Scene::EmptyTile = 0;

    if (Scene::ObjectLists == NULL) {
        Scene::ObjectLists = new HashMap<ObjectList*>(CombinedHash::EncryptData, 4);
        Scene::ObjectRegistries = new HashMap<ObjectList*>(CombinedHash::EncryptData, 16);
    }

    for (size_t i = 0; i < Scene::Layers.size(); i++)
        Scene::Layers[i].Dispose();
    Scene::Layers.clear();

    // Start reading
    if (r->ReadUInt32() != HatchSceneReader::Magic) {
        Log::Print(Log::LOG_ERROR, "Not a Hatch scene!");
        r->Close();
        return false;
    }

    // Read scene version
    Uint8 verMajor = r->ReadByte();
    Uint8 verMinor = r->ReadByte();
    Uint8 verPatch = r->ReadUInt16();

    Log::Print(Log::LOG_VERBOSE, "Scene version: %d.%d.%d", verMajor, verMinor, verPatch);

    if (verMajor != 0) {
        Log::Print(Log::LOG_ERROR, "Unrecognized scene version %d.%d.%d!", verMajor, verMinor, verPatch);
        r->Close();
        return false;
    }

    // Load the tileset
    HatchSceneReader::LoadTileset(parentFolder);

    // Load tile collisions
    HatchSceneReader::LoadTileCollisions(parentFolder);

    r->ReadUInt32(); // Editor background color 1
    r->ReadUInt32(); // Editor background color 2
    if (verPatch >= 1)
        r->ReadByte(); // ?

    // Read kits
    Uint8 numKits = r->ReadByte();

    // Read layers
    Uint8 numLayers = r->ReadByte();
    Scene::Layers.resize(numLayers);
    for (Uint32 i = 0; i < numLayers; i++) {
        SceneLayer layer = HatchSceneReader::ReadLayer(r);

#ifdef HSCN_READER_DEBUG
        Log::Print(Log::LOG_VERBOSE, "Layer %d (%s): %dx%d", i, layer.Name, layer.Width, layer.Height);
#endif

        Scene::Layers[i] = layer;
    }

    // Spawn script managers
    Scene::AddManagers();

    // Read classes and entities
    HatchSceneReader::ReadClasses(r);
    HatchSceneReader::ReadEntities(r);

    // Free classes
    HatchSceneReader::FreeClasses();

    if (Scene::PriorityLists) {
        for (int i = Scene::PriorityPerLayer - 1; i >= 0; i--)
            Scene::PriorityLists[i].Dispose();
    }
    else {
        Scene::PriorityLists = (DrawGroupList*)Memory::TrackedCalloc("Scene::PriorityLists",
            Scene::PriorityPerLayer, sizeof(DrawGroupList));

        if (!Scene::PriorityLists) {
            Log::Print(Log::LOG_ERROR, "Out of memory!");
            exit(-1);
        }
    }

    for (int i = Scene::PriorityPerLayer - 1; i >= 0; i--)
        Scene::PriorityLists[i].Init();

    return true;
}

PRIVATE STATIC SceneLayer HatchSceneReader::ReadLayer(Stream* r) {
    char* name = r->ReadHeaderedString();
    Uint8 drawBehavior = r->ReadByte();
    Uint8 drawGroup = r->ReadByte();
    Uint16 width = r->ReadUInt16();
    Uint16 height = r->ReadUInt16();

    SceneLayer layer(width, height);

    size_t nameBufLen = sizeof(layer.Name);
    memset(layer.Name, 0x00, nameBufLen);

    layer.Flags = SceneLayer::FLAGS_COLLIDEABLE | SceneLayer::FLAGS_NO_REPEAT_X | SceneLayer::FLAGS_NO_REPEAT_Y;
    layer.Visible = true;

    // Copy its name
    if (strlen(name) >= nameBufLen)
        Log::Print(Log::LOG_WARN, "Layer name '%s' is longer than max size of %u, truncating", name, nameBufLen);

    StringUtils::Copy(layer.Name, name, nameBufLen);
    Memory::Free(name);

    // Set draw group and behavior
    if (drawGroup & 0x10) {
        drawGroup &= 0xF;
        layer.Visible = false;
    }

    layer.DrawGroup = drawGroup;
    layer.DrawBehavior = drawBehavior;

    layer.RelativeY = r->ReadInt16();
    layer.ConstantY = r->ReadInt16();

    layer.ScrollInfoCount = r->ReadUInt16();
    layer.ScrollInfos = (ScrollingInfo*)Memory::Malloc(layer.ScrollInfoCount * sizeof(ScrollingInfo));

    if (!layer.ScrollInfos) {
        Log::Print(Log::LOG_ERROR, "Out of memory!");
        exit(-1);
    }

    // Read scroll data
    HatchSceneReader::ReadScrollData(r, layer);

    // Read and convert tile data
    HatchSceneReader::ReadTileData(r, layer);
    HatchSceneReader::ConvertTileData(&layer);

    memcpy(layer.TilesBackup, layer.Tiles, layer.DataSize);

    return layer;
}

PRIVATE STATIC void HatchSceneReader::ReadTileData(Stream* r, SceneLayer layer) {
    size_t streamPos = r->Position();

    Uint32 layerCompSize = r->ReadUInt32() - 4;
    Uint32 layerUncompSize = r->ReadUInt32BE();

    r->Seek(streamPos);

    Uint32 dataSize = layer.DataSize;
    if (layerUncompSize > dataSize)
        Log::Print(Log::LOG_WARN, "Layer has more stored tile data (%u) than allocated (%u)\n", layerUncompSize, dataSize);

    r->ReadCompressed(layer.Tiles, dataSize);
}

PRIVATE STATIC void HatchSceneReader::ConvertTileData(SceneLayer* layer) {
    for (size_t i = 0; i < layer->Width * layer->Height; i++) {
        if (layer->Tiles[i] == HSCN_EMPTY_TILE) {
            layer->Tiles[i] = Scene::EmptyTile;
            continue;
        }

        Uint32 tileID = (layer->Tiles[i] & HSCN_FXYID_MASK);
        Uint32 tile = (tileID & TILE_IDENT_MASK);
        if (tile >= Scene::TileSpriteInfos.size()) {
            layer->Tiles[i] = Scene::EmptyTile;
            continue;
        }

        if (layer->Tiles[i] & HSCN_FLIPX_MASK)
            tile |= TILE_FLIPX_MASK;
        if (layer->Tiles[i] & HSCN_FLIPY_MASK)
            tile |= TILE_FLIPY_MASK;

        Uint32 collA = (layer->Tiles[i] & HSCN_COLLA_MASK) >> 14;
        Uint32 collB = (layer->Tiles[i] & HSCN_COLLB_MASK) >> 16;

        tile |= (collA << 28);
        tile |= (collB << 26);

        layer->Tiles[i] = tile;
    }
}

PRIVATE STATIC void HatchSceneReader::ReadScrollData(Stream* r, SceneLayer layer) {
    for (Uint16 i = 0; i < layer.ScrollInfoCount; i++) {
        ScrollingInfo* info = &layer.ScrollInfos[i];

        info->RelativeParallax = r->ReadInt16();
        info->ConstantParallax = r->ReadInt16();
        info->CanDeform = r->ReadByte();

        r->ReadByte(); // ?

        info->Position = 0;
        info->Offset = 0;
    }

    size_t streamPos = r->Position();

    Uint32 layerCompSize = r->ReadUInt32() - 4;
    Uint32 layerUncompSize = r->ReadUInt32BE();

    r->Seek(streamPos);

    Uint32 dataSize = layer.ScrollIndexCount * 16;
    if (layerUncompSize > dataSize)
        Log::Print(Log::LOG_WARN, "Layer has more stored scroll indexes (%u) than allocated (%u)\n", layerUncompSize, dataSize);

    r->ReadCompressed(layer.ScrollIndexes, dataSize);
}

static vector<SceneClass> SceneClasses;

PRIVATE STATIC SceneClass* HatchSceneReader::FindClass(SceneHash hash) {
    for (size_t i = 0; i < SceneClasses.size(); i++) {
        if (SceneClasses[i].Hash == hash)
            return &SceneClasses[i];
    }

    return NULL;
}

PRIVATE STATIC SceneClassProperty* HatchSceneReader::FindProperty(SceneClass* scnClass, SceneHash hash) {
    for (size_t i = 0; i < scnClass->Properties.size(); i++) {
        if (scnClass->Properties[i].Hash == hash)
            return &scnClass->Properties[i];
    }

    return NULL;
}

PRIVATE STATIC void HatchSceneReader::HashString(char* string, SceneHash* hash) {
    Uint8 final[16];

    MD5::EncryptString(final, string);

    hash->A = final[0]  + (final[1]  << 8) + (final[2]  << 16) + (final[3]  << 24);
    hash->B = final[4]  + (final[5]  << 8) + (final[6]  << 16) + (final[7]  << 24);
    hash->C = final[8]  + (final[9]  << 8) + (final[10] << 16) + (final[11] << 24);
    hash->D = final[12] + (final[13] << 8) + (final[14] << 16) + (final[15] << 24);
}

PRIVATE STATIC void HatchSceneReader::ReadClasses(Stream *r) {
    Uint16 numClasses = r->ReadUInt16();

    SceneClasses.clear();

    for (Uint16 i = 0; i < numClasses; i++) {
        char* className = r->ReadHeaderedString();
        Uint8 numProps = r->ReadByte();

        SceneClass scnClass;
        scnClass.Name = className;
        scnClass.Properties.clear();

        HatchSceneReader::HashString(className, &scnClass.Hash); // Create hash

#ifdef HSCN_READER_DEBUG
        Log::Print(Log::LOG_VERBOSE, "Class: %s", className);
        Log::Print(Log::LOG_VERBOSE, "  Hash: %08x%08x%08x%08x",
            scnClass.Hash.A, scnClass.Hash.B, scnClass.Hash.C, scnClass.Hash.D);
        Log::Print(Log::LOG_VERBOSE, "  Properties: %u", numProps);
#endif

        for (Uint8 j = 0; j < numProps; j++) {
            char* propName = r->ReadHeaderedString();
            Uint8 propType = r->ReadByte();

            SceneClassProperty prop;
            prop.Name = propName;
            prop.Type = propType;

            HatchSceneReader::HashString(propName, &prop.Hash); // Create hash

#ifdef HSCN_READER_DEBUG
            static const char* propTypeNames[] = {
                "uint8", "uint16", "uint32",
                "int8", "int16", "int32",
                "enum", "bool", "string",
                "vector2", "unknown", "color"
            };

            Log::Print(Log::LOG_VERBOSE, "    Name: %s", propName);
            Log::Print(Log::LOG_VERBOSE, "    Type: %s", propTypeNames[prop.Type]);
            Log::Print(Log::LOG_VERBOSE, "    Hash: %08x%08x%08x%08x",
                prop.Hash.A, prop.Hash.B, prop.Hash.C, prop.Hash.D);
#endif

            scnClass.Properties.push_back(prop);
        }

        SceneClasses.push_back(scnClass);
    }
}

PRIVATE STATIC void HatchSceneReader::FreeClasses() {
    for (size_t i = 0; i < SceneClasses.size(); i++) {
        SceneClass* scnClass = &SceneClasses[i];
        Memory::Free(scnClass->Name);
        for (size_t j = 0; j < scnClass->Properties.size(); j++)
            Memory::Free(scnClass->Properties[j].Name);
    }

    SceneClasses.clear();
}

PRIVATE STATIC void HatchSceneReader::LoadTileset(const char* parentFolder) {
    ISprite* tileSprite = new ISprite();
    Scene::TileSprites.push_back(tileSprite);

    TileSpriteInfo info;
    Scene::TileSpriteInfos.clear();

    char tilesetFile[4096];
    snprintf(tilesetFile, sizeof(tilesetFile), "%s/Tileset.png", parentFolder);

    int cols, rows;
    tileSprite->Spritesheets[0] = tileSprite->AddSpriteSheet(tilesetFile);
    cols = tileSprite->Spritesheets[0]->Width / Scene::TileSize;
    rows = tileSprite->Spritesheets[0]->Height / Scene::TileSize;

    tileSprite->ReserveAnimationCount(1);
    tileSprite->AddAnimation("TileSprite", 0, 0, cols * rows);

    // Add tiles
    for (int i = 0; i < cols * rows; i++) {
        info.Sprite = tileSprite;
        info.AnimationIndex = 0;
        info.FrameIndex = (int)tileSprite->Animations[0].Frames.size();
        Scene::TileSpriteInfos.push_back(info);

        tileSprite->AddFrame(0,
            (i % cols) * Scene::TileSize,
            (i / cols) * Scene::TileSize,
            Scene::TileSize, Scene::TileSize, -Scene::TileSize / 2, -Scene::TileSize / 2);
    }

    Scene::EmptyTile = Scene::TileSpriteInfos.size();

    // Add empty tile
    info.Sprite = tileSprite;
    info.AnimationIndex = 0;
    info.FrameIndex = (int)tileSprite->Animations[0].Frames.size();
    Scene::TileSpriteInfos.push_back(info);

    tileSprite->AddFrame(0, 0, 0, 1, 1, 0, 0);
}

PRIVATE STATIC void HatchSceneReader::LoadTileCollisions(const char* parentFolder) {
    char tileColFile[4096];
    snprintf(tileColFile, sizeof(tileColFile), "%s/TileCol.bin", parentFolder);

    Scene::LoadTileCollisions(tileColFile);
}

PRIVATE STATIC void HatchSceneReader::ReadEntities(Stream *r) {
    Uint16 numEntities = r->ReadUInt16();

    for (Uint16 i = 0; i < numEntities; i++) {
        SceneHash classHash;
        classHash.A = r->ReadUInt32();
        classHash.B = r->ReadUInt32();
        classHash.C = r->ReadUInt32();
        classHash.D = r->ReadUInt32();

        float posX = r->ReadUInt32() / 65536.0;
        float posY = r->ReadUInt32() / 65536.0;
        Uint8 filter = r->ReadByte();
        Uint8 numProps = r->ReadByte();

        if (classHash.A == 0x19191919
         && classHash.B == 0x29292929
         && classHash.C == 0x39393939
         && classHash.D == 0x49494949) {
#ifdef HSCN_READER_DEBUG
            Log::Print(Log::LOG_WARN, "Ignoring entity with an unknown class");
#endif
            HatchSceneReader::SkipEntityProperties(r, numProps);
            continue;
        }

        // Find the class from its hash
        SceneClass* scnClass = HatchSceneReader::FindClass(classHash);
        if (!scnClass) {
#ifdef HSCN_READER_DEBUG
            Log::Print(Log::LOG_WARN,
                "Could not find any class with hash %08x%08x%08x%08x",
                classHash.A, classHash.B, classHash.C, classHash.D);
#endif
            HatchSceneReader::SkipEntityProperties(r, numProps);
            continue;
        }

        // Get the correct object list from the class name
        Uint32 objectHash = CRC32::EncryptData(&classHash.A, 16);
        char* objectName = scnClass->Name;
        ObjectList* objectList;

        if (Scene::ObjectLists->Exists(objectHash)) {
            objectList = Scene::ObjectLists->Get(objectHash);
            objectList->SpawnFunction = (Entity*(*)())BytecodeObjectManager::GetSpawnFunction(objectHash, objectName);
        } else {
            objectList = new ObjectList();
            StringUtils::Copy(objectList->ObjectName, objectName, sizeof(objectList->ObjectName));
            Scene::ObjectLists->Put(objectHash, objectList);
            objectList->SpawnFunction = (Entity*(*)())BytecodeObjectManager::GetSpawnFunction(objectHash, objectName);

            char objLoadFunc[sizeof(objectList->ObjectName) + 6];
            snprintf(objLoadFunc, sizeof(objLoadFunc), "%s_Load", objectList->ObjectName);
            BytecodeObjectManager::CallFunction(objLoadFunc);
        }

        // Spawn the object, if the class exists
        if (objectList->SpawnFunction) {
            BytecodeObject* obj = (BytecodeObject*)objectList->SpawnFunction();
            obj->X = posX;
            obj->Y = posY;
            obj->InitialX = posX;
            obj->InitialY = posY;
            obj->List = objectList;
            Scene::AddStatic(objectList, obj);

            // Add "filter" property
            obj->Properties->Put("filter", INTEGER_VAL(filter));

            // Add all properties
            for (Uint8 j = 0; j < numProps; j++) {
                SceneHash propHash;
                propHash.A = r->ReadUInt32();
                propHash.B = r->ReadUInt32();
                propHash.C = r->ReadUInt32();
                propHash.D = r->ReadUInt32();

                Uint8 varType = r->ReadByte();

                // Find the class property from the hash
                SceneClassProperty* classProp = HatchSceneReader::FindProperty(scnClass, propHash);
                if (!classProp) {
#ifdef HSCN_READER_DEBUG
                    Log::Print(Log::LOG_WARN,
                        "Could not find any property with hash %08x%08x%08x%08x",
                        propHash.A, propHash.B, propHash.C, propHash.D);
#endif
                    HatchSceneReader::SkipProperty(r, varType);
                    continue;
                }

                // Add the property
                Uint8 u8Var;
                Uint16 u16Var;
                Uint32 u32Var;
                Uint32 vecVar[2];
                Uint16 strLength;
                char* strVar;

                VMValue val = NULL_VAL;

                switch (varType) {
                case HSCN_VAR_INT8:
                case HSCN_VAR_UINT8:
                    r->ReadBytes(&u8Var, 1);
                    val = INTEGER_VAL(u8Var);
                    break;
                case HSCN_VAR_INT16:
                case HSCN_VAR_UINT16:
                    r->ReadBytes(&u16Var, 2);
                    val = INTEGER_VAL(u16Var);
                    break;
                case HSCN_VAR_ENUM:
                case HSCN_VAR_BOOL:
                case HSCN_VAR_COLOR:
                case HSCN_VAR_INT32:
                case HSCN_VAR_UINT32:
                case HSCN_VAR_UNKNOWN:
                    r->ReadBytes(&u32Var, 4);
                    val = INTEGER_VAL((int)u32Var);
                    break;
                case HSCN_VAR_VECTOR2:
                    r->ReadBytes(vecVar, 8);
                    {
                        float fx = vecVar[0] / 65536.0;
                        float fy = vecVar[1] / 65536.0;

                        VMValue valX = DECIMAL_VAL(fx);
                        VMValue valY = DECIMAL_VAL(fy);

                        ObjArray* array = NewArray();
                        array->Values->push_back(valX);
                        array->Values->push_back(valY);

                        val = OBJECT_VAL(array);
                    }
                    break;
                case HSCN_VAR_STRING:
                    strLength = r->ReadUInt16();
                    strVar = (char*)malloc(strLength);
                    if (!strVar) {
                        Log::Print(Log::LOG_ERROR, "Out of memory!");
                        exit(-1);
                    }

                    for (size_t i = 0; i < strLength; i++)
                        strVar[i] = r->ReadInt16();

                    val = OBJECT_VAL(CopyString(strVar, strLength));
                    free(strVar);
                    break;
                }

                obj->Properties->Put(classProp->Name, val);
            }
        }
        else
            HatchSceneReader::SkipEntityProperties(r, numProps);
    }
}

PRIVATE STATIC void HatchSceneReader::SkipEntityProperties(Stream *r, Uint8 numProps) {
    for (Uint8 j = 0; j < numProps; j++) {
        r->ReadUInt32();
        r->ReadUInt32();
        r->ReadUInt32();
        r->ReadUInt32();
        HatchSceneReader::SkipProperty(r, r->ReadByte());
    }
}

PRIVATE STATIC void HatchSceneReader::SkipProperty(Stream *r, Uint8 varType) {
    switch (varType) {
    case HSCN_VAR_INT8:
    case HSCN_VAR_UINT8:
        r->Skip(1);
        break;
    case HSCN_VAR_INT16:
    case HSCN_VAR_UINT16:
        r->Skip(2);
        break;
    case HSCN_VAR_ENUM:
    case HSCN_VAR_BOOL:
    case HSCN_VAR_COLOR:
    case HSCN_VAR_INT32:
    case HSCN_VAR_UINT32:
    case HSCN_VAR_UNKNOWN:
        r->Skip(4);
        break;
    case HSCN_VAR_VECTOR2:
        r->Skip(8);
        break;
    case HSCN_VAR_STRING:
        r->Skip(r->ReadUInt16() * 2);
        break;
    }
}
