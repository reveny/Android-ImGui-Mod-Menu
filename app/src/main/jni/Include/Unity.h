#include "Math/Vector3.hpp"
#include "Math/Quaternion.hpp"

float NormalizeAngle (float angle){
    while (angle>360)
        angle -= 360;
    while (angle<0)
        angle += 360;
    return angle;
}

Vector3 NormalizeAngles (Vector3 angles){
    angles.X = NormalizeAngle (angles.X);
    angles.Y = NormalizeAngle (angles.Y);
    angles.Z = NormalizeAngle (angles.Z);
    return angles;
}

bool compareVectorsWithTolerance(Vector3 first, Vector3 second, float tolerance){
    float firstXSubbed = first.X - tolerance;
    float firstXAdded = first.X + tolerance;

    float firstYSubbed = first.Y - tolerance;
    float firstYAdded = first.Y + tolerance;

    float firstZSubbed = first.Z - tolerance;
    float firstZAdded = first.Z + tolerance;

    bool secondXFallsBetween = second.X >= firstXSubbed && second.X <= firstXAdded;
    bool secondYFallsBetween = second.Y >= firstYSubbed && second.Y <= firstYAdded;
    bool secondZFallsBetween = second.Z >= firstZSubbed && second.Z <= firstZAdded;

    return secondXFallsBetween && secondYFallsBetween && secondZFallsBetween;
}

Quaternion GetRotationToLocation(Vector3 targetLocation, float y_bias, Vector3 myLoc){
    return Quaternion::LookRotation((targetLocation + Vector3(0, y_bias, 0)) - myLoc, Vector3(0, 1, 0));
}

typedef struct _monoString {
    void *klass;
    void *monitor;
    int length;
    char16_t chars[1];

    int getLength() {
        return length;
    }

    char16_t *getRawChars() {
        return chars;
    }
} monoString;

template <typename T>
struct monoArray
{
    void* klass;
    void* monitor;
    void* bounds;
    int   max_length;
    void* vector [1];

    int getLength()
    {
        return max_length;
    }
    T getPointer()
    {
        return (T)vector;
    }
};

template <typename T>
struct monoList {
    void *unk0;
    void *unk1;
    monoArray<T> *items;
    int size;
    int version;

    T getItems() {
        return items->getPointer();
    }

    int getSize() {
        return size;
    }

    int getVersion() {
        return version;
    }
};

template <typename K, typename V>
struct monoDictionary {
    void *unk0;
    void *unk1;
    monoArray<int **> *table;
    monoArray<void **> *linkSlots;
    monoArray<K> *keys;
    monoArray<V> *values;
    int touchedSlots;
    int emptySlot;
    int size;

    K getKeys() {
        return keys->getPointer();
    }

    V getValues() {
        return values->getPointer();
    }

    int getNumKeys() {
        return keys->getLength();
    }

    int getNumValues() {
        return values->getLength();
    }

    int getSize() {
        return size;
    }
};