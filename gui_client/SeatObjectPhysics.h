#pragma once

#include "VehiclePhysics.h"
#include "PlayerPhysics.h"
#include "PhysicsObject.h"
#include "PlayerPhysicsInput.h"
#include "Scripting.h"

/**
 * A "vehicle" that you can only sit on, with no movement or interaction.
 */
class SeatObjectPhysics final : public VehiclePhysics
{
public:
    GLARE_ALIGNED_16_NEW_DELETE

    SeatObjectPhysics(WorldObjectRef object, const Scripting::VehicleScriptedSettings& script_settings)
    {
        world_object = object.ptr();
        this->script_settings = script_settings;
    }

    WorldObject* getControlledObject() override { return world_object; }

    void startRightingVehicle() override {}
    void userEnteredVehicle(int seat_index) override {}
    void userExitedVehicle(int old_seat_index) override {}

    VehiclePhysicsUpdateEvents update(PhysicsWorld&, const PlayerPhysicsInput&, float) override
    {
        // No movement or interaction, just sitting.
        return VehiclePhysicsUpdateEvents();
    }

    Vec4f getFirstPersonCamPos(PhysicsWorld&, uint32, bool) const override
    {
        // Use the seat's camera position, similar to other vehicles
        return Vec4f(0, 0, 0, 1);
    }

    Vec4f getThirdPersonCamTargetTranslation() const override
    {
        return Vec4f(0, 0, 0, 1);
    }

    float getThirdPersonCamTraceSelfAvoidanceDist() const override { return 1.0f; }
    Matrix4f getBodyTransform(PhysicsWorld&) const override { return Matrix4f::identity(); }

    Matrix4f getSeatToWorldTransform(PhysicsWorld&, uint32, bool) const override
    {
        // Provide transform for sitting position
        return Matrix4f::identity();
    }

    Vec4f getLinearVel(PhysicsWorld&) const override { return Vec4f(0, 0, 0, 0); }
    JPH::BodyID getBodyID() const override { return JPH::BodyID(); }
    const Scripting::VehicleScriptedSettings& getSettings() const override { return script_settings; }

private:
    WorldObject* world_object;
    Scripting::VehicleScriptedSettings script_settings;
};
