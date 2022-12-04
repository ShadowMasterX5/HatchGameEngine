#ifndef ENGINE_BYTECODE_GARBAGECOLLECTOR_H
#define ENGINE_BYTECODE_GARBAGECOLLECTOR_H

#define PUBLIC
#define PRIVATE
#define PROTECTED
#define STATIC
#define VIRTUAL
#define EXPOSED


#include <Engine/Bytecode/Types.h>
#include <Engine/Includes/HashMap.h>

class GarbageCollector {
public:
    static vector<Obj*> GrayList;
    static Obj*         RootObject;
    static size_t       NextGC;
    static size_t       GarbageSize;
    static double       MaxTimeAlotted;
    static bool         Print;
    static bool         FilterSweepEnabled;
    static int          FilterSweepType;

    static void Collect();
    static void GrayValue(VMValue value);
    static void GrayObject(void* obj);
    static void GrayHashMapItem(Uint32, VMValue value);
    static void GrayHashMap(void* pointer);
    static void BlackenObject(Obj* object);
    static void RemoveWhiteHashMapItem(Uint32, VMValue value);
    static void RemoveWhiteHashMap(void* pointer);
};

#endif /* ENGINE_BYTECODE_GARBAGECOLLECTOR_H */
