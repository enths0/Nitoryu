// Nitoryu runtime helpers. VS2010 (v100) safe.
//
// Spatial queries that return hands, not raw pointers: removing or changing
// one object can remove or replace others, so re-resolve each hand just
// before acting on it (hand::getBuilding() returns NULL once it is gone).
#pragma once

#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/RootObject.h>
#include <kenshi/util/hand.h>
#include <kenshi/util/lektor.h>
#include <stdint.h>
#include <vector>

namespace kit
{
    // Every object of `type` (e.g. BUILDING) within `radius` world units of
    // `pos`, all factions included; filter by getFaction()/getTown() yourself.
    // The lektor the game fills has no destructor, so its buffer is left to
    // the game's allocator (as Shutori does): never free it from the plugin.
    inline std::vector<hand> objectsNear(const Ogre::Vector3& pos, float radius, itemType type, int maxNumber)
    {
        std::vector<hand> out;
        lektor<RootObject*> results;
        ou->getObjectsWithinSphere(results, pos, radius, type, maxNumber, NULL);
        for (uint32_t i = 0; i < results.size(); ++i)
        {
            RootObject* o = results[i];
            if (o)
                out.push_back(o->getHandle());
        }
        return out;
    }
}
