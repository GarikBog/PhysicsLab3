#ifndef GAME
#define GAME

#include "PhyObject.h"
#include <vector>

enum class GameState
{
    Primitive,
    CCD,
    MultyRate
};
namespace Game
{
    struct PhysicsMetrics
    {
        double frameMilliseconds = 0.0;
        double lastStepMilliseconds = 0.0;
        unsigned int steps = 0;
        unsigned int slowSubsteps = 0;
        unsigned int mediumSubsteps = 0;
        unsigned int fastSubsteps = 0;
        unsigned int ccdContacts = 0;
        unsigned int ccdLimitHits = 0;
    };

    extern PhysicsMetrics physicsMetrics;
    void BeginPhysicsFrame();
    void AdvancePhysics();

    extern GameState gameState;
    extern float TimeDelta;
    extern std::vector<Button*> buttons;
    extern std::vector<Object*> objects;
    extern std::vector<PhyObject*> fastObjects;
    extern std::vector<PhyObject*> mediumObjects;
    extern std::vector<PhyObject*> slowObjects;
    extern std::vector<PhyObject*> staticObjects;

    void AddMeToButtons(Button* button);
    void RemoveMeFromButtons(Button* button);
    void AddMeToObjectsObjects(Object* object);
    void RemoveMeFromObjectsObjects(Object* object);
    void AddMeToPhyObjects(PhyObject* phyObject);
    void RemoveMeFromPhyObjects(PhyObject* phyObject);
    void MovePhyObjectToSpeedGroup(PhyObject* phyObject, SpeedState previousState);

    std::vector<PhyObject*> GetPhyObjectsSnapshot(bool includeStatic = true);
    void PrimitiveCalcAllPhysics();
    void CCDCalcAllPhysics();
    void MultyRateCalcPhysics();
}

#endif // GAME
