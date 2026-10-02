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
