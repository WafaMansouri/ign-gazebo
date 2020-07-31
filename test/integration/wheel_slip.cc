/*
 * Copyright (C) 2020 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

#include <gtest/gtest.h>

#include <ignition/msgs/entity_factory.pb.h>

#include <ignition/common/Console.hh>
#include <ignition/math/Pose3.hh>
#include <ignition/transport/Node.hh>

#include <sdf/Sphere.hh>
#include <sdf/Cylinder.hh>

#include "ignition/gazebo/components/Collision.hh"
#include "ignition/gazebo/components/Geometry.hh"
#include "ignition/gazebo/components/Gravity.hh"
#include "ignition/gazebo/components/Inertial.hh"
#include "ignition/gazebo/components/Joint.hh"
#include "ignition/gazebo/components/JointVelocity.hh"
#include "ignition/gazebo/components/JointVelocityCmd.hh"
#include "ignition/gazebo/components/Light.hh"
#include "ignition/gazebo/components/LinearVelocity.hh"
#include "ignition/gazebo/components/Link.hh"
#include "ignition/gazebo/components/Model.hh"
#include "ignition/gazebo/components/Name.hh"
#include "ignition/gazebo/components/ParentEntity.hh"
#include "ignition/gazebo/components/Pose.hh"
#include "ignition/gazebo/components/SlipComplianceCmd.hh"
#include "ignition/gazebo/components/World.hh"
#include "ignition/gazebo/Server.hh"
#include "ignition/gazebo/SystemLoader.hh"
#include "ignition/gazebo/test_config.hh"

#include "plugins/MockSystem.hh"

using namespace ignition;
using namespace gazebo;

/// \brief Test DiffDrive system
class WheelSlipTest : public ::testing::Test
{
  // Documentation inherited
  protected: void SetUp() override
  {
    common::Console::SetVerbosity(4);
    setenv("IGN_GAZEBO_SYSTEM_PLUGIN_PATH",
           (std::string(PROJECT_BINARY_PATH) + "/lib").c_str(), 1);
  }
};

class Relay
{
  public: Relay()
  {
    auto plugin = loader.LoadPlugin("libMockSystem.so",
                                "ignition::gazebo::MockSystem",
                                nullptr);
    EXPECT_TRUE(plugin.has_value());

    this->systemPtr = plugin.value();

    this->mockSystem = static_cast<MockSystem *>(
        systemPtr->QueryInterface<System>());
    EXPECT_NE(nullptr, this->mockSystem);
  }

  public: Relay &OnPreUpdate(MockSystem::CallbackType _cb)
  {
    this->mockSystem->preUpdateCallback = std::move(_cb);
    return *this;
  }

  public: Relay &OnUpdate(MockSystem::CallbackType _cb)
  {
    this->mockSystem->updateCallback = std::move(_cb);
    return *this;
  }

  public: Relay &OnPostUpdate(MockSystem::CallbackTypeConst _cb)
  {
    this->mockSystem->postUpdateCallback = std::move(_cb);
    return *this;
  }

  public: SystemPluginPtr systemPtr;

  private: SystemLoader loader;
  private: MockSystem *mockSystem;
};

TEST_F(WheelSlipTest, TireDrum)
{
  const double metersPerMile = 1609.34;
  const double secondsPerHour = 3600.0;
  std::vector<std::string> linksToCheck{"wheel", "axle", "upright"};

  ServerConfig serverConfig;
  const auto sdfFile = std::string(PROJECT_SOURCE_PATH) +
    "/test/worlds/tire_drum.sdf";
  serverConfig.SetSdfFile(sdfFile);

  Server server(serverConfig);
  EXPECT_FALSE(server.Running());
  EXPECT_FALSE(*server.Running(0));

  gazebo::EntityComponentManager *ecm = nullptr;
  Relay testSystem;
  testSystem.OnPreUpdate([&](const gazebo::UpdateInfo &,
                             gazebo::EntityComponentManager &_ecm)
      {
        ecm = &_ecm;
      });

  // Create a system that records the vehicle poses
  std::vector<math::Pose3d> poses;
  server.AddSystem(testSystem.systemPtr);

  // Run server and check we have the ECM
  EXPECT_EQ(nullptr, ecm);
  server.Run(true, 1, false);
  EXPECT_NE(nullptr, ecm);

  // Get world and gravity
  Entity worldEntity =
    ecm->EntityByComponents(components::World());

  EXPECT_NE(gazebo::kNullEntity, worldEntity);

  // Get both models
  Entity tireEntity =
    ecm->EntityByComponents(components::Model(),
        components::Name("tire"));

  EXPECT_NE(gazebo::kNullEntity, tireEntity);

  Entity wheelLinkEntity = ecm->EntityByComponents(
      components::ParentEntity(tireEntity),
      components::Name("wheel"),
      components::Link());

  EXPECT_NE(gazebo::kNullEntity, wheelLinkEntity);

  auto wheelInertialComp =
    ecm->Component<components::Inertial>(wheelLinkEntity);

  EXPECT_NE(nullptr, wheelInertialComp);

  const double wheelMass = wheelInertialComp->Data().MassMatrix().Mass();

  auto collisionGeometry =
    ecm->Component<components::Geometry>(wheelLinkEntity);

  ASSERT_NE(nullptr, collisionGeometry);

  ASSERT_TRUE(
      (collisionGeometry->Data().Type() == sdf::GeometryType::SPHERE) ||
      (collisionGeometry->Data().Type() == sdf::GeometryType::CYLINDER));

  double wheelRadius = 0.0;

  if (collisionGeometry->Data().Type() == sdf::GeometryType::SPHERE)
    wheelRadius = collisionGeometry->Data().SphereShape()->Radius();
  if (collisionGeometry->Data().Type() == sdf::GeometryType::CYLINDER)
    wheelRadius = collisionGeometry->Data().CylinderShape()->Radius();

  auto collisionComp =
    ecm->Component<components::CollisionElement>(wheelLinkEntity);

  ASSERT_NE(nullptr, collisionComp);

  // TODO(anyone) enable below tests when kp is supported
  // auto elem = collisionComp->Data().Element();
  // ASSERT_TRUE(elem->HasElement("surface"));
  // auto surface = elem->GetElement("surface");
  // ASSERT_NE(nullptr, surface);
  // auto surfaceContact = surface->GetElement("contact");
  // auto surfaceContactOde = surfaceContact->GetElement("ode");
  // const double kp = surfaceContactOde->GetElement("kp")->Get<double>();
  // ASSERT_EQ(kp, 250e3);

  double modelMass = 0.0;
  for (const auto &linkName : linksToCheck)
  {
    Entity linkEntity = ecm->EntityByComponents(
        components::ParentEntity(tireEntity),
        components::Name(linkName),
        components::Link());
    EXPECT_NE(gazebo::kNullEntity, linkEntity);
    auto inertialComp = ecm->Component<components::Inertial>(linkEntity);

    EXPECT_NE(nullptr, inertialComp);

    modelMass += inertialComp->Data().MassMatrix().Mass();
  }

  // Get drum radius
  Entity drumEntity =
    ecm->EntityByComponents(components::Model(),
        components::Name("drum"));

  ASSERT_NE(gazebo::kNullEntity, drumEntity);

  Entity drumJointEntity = ecm->EntityByComponents(
      components::ParentEntity(drumEntity),
      components::Name("joint"),
      components::Joint());

  ASSERT_NE(gazebo::kNullEntity, drumJointEntity);

  Entity drumLinkEntity = ecm->EntityByComponents(
      components::ParentEntity(drumEntity),
      components::Name("link"),
      components::Link());

  ASSERT_NE(gazebo::kNullEntity, drumLinkEntity);
  auto drumCollisionGeometry =
    ecm->Component<components::Geometry>(wheelLinkEntity);

  ASSERT_NE(nullptr, drumCollisionGeometry);
  ASSERT_EQ(sdf::GeometryType::CYLINDER, drumCollisionGeometry->Data().Type());
  const double drumRadius =
    drumCollisionGeometry->Data().CylinderShape()->Radius();

  // Get axle wheel and steer joint of wheel model

  Entity wheelAxleJointEntity = ecm->EntityByComponents(
      components::ParentEntity(tireEntity),
      components::Name("axle_wheel"),
      components::Joint());

  ASSERT_NE(gazebo::kNullEntity, wheelAxleJointEntity);

  Entity wheelSteerJointEntity = ecm->EntityByComponents(
      components::ParentEntity(tireEntity),
      components::Name("steer"),
      components::Joint());

  ASSERT_NE(gazebo::kNullEntity, wheelSteerJointEntity);

  const double drumSpeed = -25.0 * metersPerMile / secondsPerHour / drumRadius;
  const double wheelSpeed =
    -25.0 * metersPerMile / secondsPerHour / wheelRadius;

  math::Vector2d slipCmd;
  double wheelNormalForce = 1000.0;

  double wheelSlip1 = 0.0;
  double wheelSlip2 = 0.0;

  double slipComplianceLateral = 0.1;
  double slipComplianceLongitudinal = 0.0;

  // Zero slip
  components::SlipComplianceCmd newSlipCmdComp({wheelSlip1, wheelSlip2});

  Entity collisionEntity = ecm->EntityByComponents(
      components::ParentEntity(tireEntity),
      components::Name("collision"),
      components::Collision());

  auto currSlipCmdComp =
    ecm->Component<components::SlipComplianceCmd>(collisionEntity);

  if (currSlipCmdComp)
    *currSlipCmdComp = newSlipCmdComp;
  else
    ecm->CreateComponent(collisionEntity, newSlipCmdComp);

  // Lateral slip: low
  wheelSlip1 = wheelSpeed / wheelNormalForce * slipComplianceLateral;
  wheelSlip2 = wheelSpeed / wheelNormalForce * slipComplianceLongitudinal;
  newSlipCmdComp = components::SlipComplianceCmd({wheelSlip1, wheelSlip2});
  ecm->CreateComponent(collisionEntity, newSlipCmdComp);

  // Lateral slip: high
  slipComplianceLateral = 1.0;
  wheelSlip1 = wheelSpeed / wheelNormalForce * slipComplianceLateral;
  wheelSlip2 = wheelSpeed / wheelNormalForce * slipComplianceLongitudinal;
  newSlipCmdComp = components::SlipComplianceCmd({wheelSlip1, wheelSlip2});
  ecm->CreateComponent(collisionEntity, newSlipCmdComp);
}

TEST_F(WheelSlipTest, TricyclesUphill)
{
  ServerConfig serverConfig;
  const auto sdfFile = std::string(PROJECT_SOURCE_PATH) +
    "/test/worlds/trisphere_cycle_wheel_slip.sdf";
  serverConfig.SetSdfFile(sdfFile);

  Server server(serverConfig);
  EXPECT_FALSE(server.Running());
  EXPECT_FALSE(*server.Running(0));

  gazebo::EntityComponentManager *ecm = nullptr;
  Relay testSystem;
  testSystem.OnPreUpdate([&](const gazebo::UpdateInfo &,
                             gazebo::EntityComponentManager &_ecm)
      {
        ecm = &_ecm;
      });

  // Create a system that records the vehicle poses
  std::vector<math::Pose3d> poses;
  server.AddSystem(testSystem.systemPtr);

  // Run server and check we have the ECM
  EXPECT_EQ(nullptr, ecm);
  server.Run(true, 1, false);
  EXPECT_NE(nullptr, ecm);

  // Get world and gravity
  Entity worldEntity =
    ecm->EntityByComponents(components::World());

  EXPECT_NE(gazebo::kNullEntity, worldEntity);

  auto gravity = ecm->Component<components::Gravity>(worldEntity);

  EXPECT_NE(nullptr, gravity);
  EXPECT_EQ(math::Vector3d(-2, 0, -9.8), gravity->Data());

  // Get both models
  Entity trisphereCycle0Entity =
    ecm->EntityByComponents(components::Model(),
        components::Name("trisphere_cycle0"));

  EXPECT_NE(gazebo::kNullEntity, trisphereCycle0Entity);

  Entity trisphereCycle1Entity =
    ecm->EntityByComponents(components::Model(),
        components::Name("trisphere_cycle1"));

  EXPECT_NE(gazebo::kNullEntity, trisphereCycle1Entity);

  // Check rear left wheel of first model
  Entity wheelRearLeftEntity = ecm->EntityByComponents(
      components::ParentEntity(trisphereCycle0Entity),
      components::Name("wheel_rear_left"),
      components::Link());

  EXPECT_NE(gazebo::kNullEntity, wheelRearLeftEntity);

  Entity wheelRearLeftCollisionEntity = ecm->EntityByComponents(
      components::ParentEntity(wheelRearLeftEntity),
      components::Collision());

  auto collisionGeometry =
    ecm->Component<components::Geometry>(wheelRearLeftCollisionEntity);
  EXPECT_EQ(sdf::GeometryType::SPHERE, collisionGeometry->Data().Type());
  EXPECT_NE(nullptr, collisionGeometry->Data().SphereShape());

  const double wheelRadius = collisionGeometry->Data().SphereShape()->Radius();
  EXPECT_DOUBLE_EQ(0.15, wheelRadius);

  // Get rear wheel spins of both models
  Entity wheelRearLeftSpin0Entity = ecm->EntityByComponents(
      components::ParentEntity(trisphereCycle0Entity),
      components::Name("wheel_rear_left_spin"),
      components::Joint());

  EXPECT_NE(gazebo::kNullEntity, wheelRearLeftSpin0Entity);

  Entity wheelRearRightSpin0Entity = ecm->EntityByComponents(
      components::ParentEntity(trisphereCycle0Entity),
      components::Name("wheel_rear_right_spin"),
      components::Joint());

  EXPECT_NE(gazebo::kNullEntity, wheelRearRightSpin0Entity);

  Entity wheelRearLeftSpin1Entity = ecm->EntityByComponents(
      components::ParentEntity(trisphereCycle1Entity),
      components::Name("wheel_rear_left_spin"),
      components::Joint());

  EXPECT_NE(gazebo::kNullEntity, wheelRearLeftSpin1Entity);

  Entity wheelRearRightSpin1Entity = ecm->EntityByComponents(
      components::ParentEntity(trisphereCycle1Entity),
      components::Name("wheel_rear_right_spin"),
      components::Joint());

  EXPECT_NE(gazebo::kNullEntity, wheelRearRightSpin1Entity);

  // Set speed of both models
  const double angularSpeed = 6.0;

  ecm->CreateComponent(
        wheelRearLeftSpin0Entity,
        components::JointVelocityCmd({angularSpeed}));

  ecm->CreateComponent(
        wheelRearRightSpin0Entity,
        components::JointVelocityCmd({angularSpeed}));

  ecm->CreateComponent(
        wheelRearLeftSpin1Entity,
        components::JointVelocityCmd({angularSpeed}));

  ecm->CreateComponent(
        wheelRearRightSpin1Entity,
        components::JointVelocityCmd({angularSpeed}));

  server.Run(true, 2000, false);

  // compute expected slip
  // normal force as passed to Wheel Slip in test world
  const double wheelNormalForce = 32;
  const double mass = 14.5;
  const double forceRatio =
    (mass/2) * std::abs(gravity->Data().X()) / wheelNormalForce;
  const double noSlipLinearSpeed = wheelRadius * angularSpeed;

  auto wheelRearLeftVelocity =
    ecm->Component<components::JointVelocity>(wheelRearLeftSpin0Entity);
  auto wheelRearRightVelocity =
    ecm->Component<components::JointVelocity>(wheelRearRightSpin0Entity);
  auto worldVel =
    ecm->Component<components::WorldLinearVelocity>(trisphereCycle0Entity);

  if (!worldVel)
  {
    ecm->CreateComponent(trisphereCycle0Entity,
        components::WorldLinearVelocity());
    worldVel =
      ecm->Component<components::WorldLinearVelocity>(trisphereCycle0Entity);
  }

  EXPECT_NE(nullptr, wheelRearLeftVelocity);
  EXPECT_NE(nullptr, wheelRearRightVelocity);
  EXPECT_NE(nullptr, worldVel);

  EXPECT_NEAR(angularSpeed, wheelRearLeftVelocity->Data()[0], 3e-3);
  EXPECT_NEAR(angularSpeed, wheelRearRightVelocity->Data()[0], 3e-3);
  EXPECT_NEAR(noSlipLinearSpeed - worldVel->Data()[0], 0.0, 5e-3);

  wheelRearLeftVelocity =
    ecm->Component<components::JointVelocity>(wheelRearLeftSpin1Entity);
  wheelRearRightVelocity =
    ecm->Component<components::JointVelocity>(wheelRearRightSpin1Entity);
  worldVel =
     ecm->Component<components::WorldLinearVelocity>(trisphereCycle1Entity);

  if (!worldVel)
  {
    ecm->CreateComponent(trisphereCycle1Entity,
        components::WorldLinearVelocity());
    worldVel =
      ecm->Component<components::WorldLinearVelocity>(trisphereCycle1Entity);
  }

  EXPECT_NE(nullptr, wheelRearLeftVelocity);
  EXPECT_NE(nullptr, wheelRearRightVelocity);
  EXPECT_NE(nullptr, worldVel);

  EXPECT_NEAR(angularSpeed, wheelRearLeftVelocity->Data()[0], 3e-3);
  EXPECT_NEAR(angularSpeed, wheelRearRightVelocity->Data()[0], 3e-3);
  EXPECT_NEAR(noSlipLinearSpeed - worldVel->Data()[0],
      noSlipLinearSpeed * forceRatio, 5e-3);
}
