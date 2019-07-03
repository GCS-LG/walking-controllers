/**
 * @file WalkingModule.cpp
 * @authors Giulio Romualdi <giulio.romualdi@iit.it>
 * @copyright 2018 iCub Facility - Istituto Italiano di Tecnologia
 *            Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 * @date 2018
 */

// std
#include <iostream>
#include <memory>

// YARP
#include <yarp/os/RFModule.h>
#include <yarp/os/BufferedPort.h>
#include <yarp/sig/Vector.h>
#include <yarp/os/LogStream.h>

// iDynTree
#include <iDynTree/Core/VectorFixSize.h>
#include <iDynTree/Core/EigenHelpers.h>
#include <iDynTree/yarp/YARPConversions.h>
#include <iDynTree/yarp/YARPEigenConversions.h>
#include <iDynTree/Model/Model.h>
#include <iDynTree/Core/Wrench.h>

#include <WalkingModule.hpp>
#include <Utils.hpp>

double timeOffset;

double impactTimeNominal, impactTimeAdjusted;
iDynTree::Vector2 zmpNominal, zmpAdjusted;

void WalkingModule::propagateTime()
{
    // propagate time
    m_time += m_dT;
}

bool WalkingModule::advanceReferenceSignals()
{
    // check if vector is not initialized
    if(m_leftTrajectory.empty()
       || m_rightTrajectory.empty()
       || m_leftInContact.empty()
       || m_rightInContact.empty()
       || m_DCMPositionDesired.empty()
       || m_DCMVelocityDesired.empty()
       || m_comHeightTrajectory.empty())
    {
        yError() << "[WalkingModule::advanceReferenceSignals] Cannot advance empty reference signals.";
        return false;
    }

    m_rightTrajectory.pop_front();
    m_rightTrajectory.push_back(m_rightTrajectory.back());

    m_leftTrajectory.pop_front();
    m_leftTrajectory.push_back(m_leftTrajectory.back());

    m_rightTwistTrajectory.pop_front();
    m_rightTwistTrajectory.push_back(m_rightTwistTrajectory.back());

    m_leftTwistTrajectory.pop_front();
    m_leftTwistTrajectory.push_back(m_leftTwistTrajectory.back());

    m_rightAccelerationTrajectory.pop_front();
    m_rightAccelerationTrajectory.push_back(m_rightAccelerationTrajectory.back());

    m_leftAccelerationTrajectory.pop_front();
    m_leftAccelerationTrajectory.push_back(m_leftAccelerationTrajectory.back());

    m_rightInContact.pop_front();
    m_rightInContact.push_back(m_rightInContact.back());

    m_leftInContact.pop_front();
    m_leftInContact.push_back(m_leftInContact.back());

    m_isLeftFixedFrame.pop_front();
    m_isLeftFixedFrame.push_back(m_isLeftFixedFrame.back());

    m_ZMPPositionDesired.pop_front();
    m_ZMPPositionDesired.push_back(m_ZMPPositionDesired.back());

    m_DCMPositionDesired.pop_front();
    m_DCMPositionDesired.push_back(m_DCMPositionDesired.back());

    m_DCMVelocityDesired.pop_front();
    m_DCMVelocityDesired.push_back(m_DCMVelocityDesired.back());

    m_comHeightTrajectory.pop_front();
    m_comHeightTrajectory.push_back(m_comHeightTrajectory.back());

    m_comHeightVelocity.pop_front();
    m_comHeightVelocity.push_back(m_comHeightVelocity.back());

    m_weightInLeft.pop_front();
    m_weightInLeft.push_back(m_weightInLeft.back());

    m_weightInRight.pop_front();
    m_weightInRight.push_back(m_weightInRight.back());

    // at each sampling time the merge points are decreased by one.
    // If the first merge point is equal to 0 it will be dropped.
    // A new trajectory will be merged at the first merge point or if the deque is empty
    // as soon as possible.
    if(!m_mergePoints.empty())
    {
        for(auto& mergePoint : m_mergePoints)
            mergePoint--;

        if(m_mergePoints[0] == 0)
            m_mergePoints.pop_front();
    }
    return true;
}

double WalkingModule::getPeriod()
{
    //  period of the module (seconds)
    return m_dT;
}

bool WalkingModule::setRobotModel(const yarp::os::Searchable& rf)
{
    // load the model in iDynTree::KinDynComputations
    std::string model = rf.check("model",yarp::os::Value("model.urdf")).asString();
    std::string pathToModel = yarp::os::ResourceFinder::getResourceFinderSingleton().findFileByName(model);

    yInfo() << "[WalkingModule::setRobotModel] The model is found in: " << pathToModel;

    // only the controlled joints are extracted from the URDF file
    if(!m_loader.loadReducedModelFromFile(pathToModel, m_robotControlHelper->getAxesList()))
    {
        yError() << "[WalkingModule::setRobotModel] Error while loading the model from " << pathToModel;
        return false;
    }
    return true;
}

bool WalkingModule::configure(yarp::os::ResourceFinder& rf)
{
    // TODO REMOVE ME
    impactTimeNominal = 0;
    impactTimeAdjusted = 0;

    zmpNominal.zero();
    zmpAdjusted.zero();

    m_nominalValuesLeft.zero();
    m_nominalValuesRight.zero();
    m_currentValues.zero();

    m_adaptatedFootLeftTwist.zero();
    m_adaptatedFootRightTwist.zero();
    m_currentFootLeftTwist.zero();
    m_currentFootRightTwist.zero();

    iDynTree::Position tempTemp;
    iDynTree::Rotation tempRot;
    tempRot.Identity();
    //    tempRot.
    tempTemp.zero();
    m_adaptatedFootLeftTransform.setPosition(tempTemp);
    m_currentFootLeftTransform.setPosition(tempTemp);

    m_adaptatedFootRightTransform.setPosition(tempTemp);
    m_currentFootRightTransform.setPosition(tempTemp);

    m_currentFootLeftTransform.setRotation(tempRot);
    m_adaptatedFootLeftTransform.setRotation(tempRot);

    m_currentFootRightTransform.setRotation(tempRot);
    m_adaptatedFootRightTransform.setRotation(tempRot);

    // module name (used as prefix for opened ports)
    m_useStepAdaptation = rf.check("use_step_adaptation", yarp::os::Value(false)).asBool();
    m_useMPC = rf.check("use_mpc", yarp::os::Value(false)).asBool();
    m_useQPIK = rf.check("use_QP-IK", yarp::os::Value(false)).asBool();
    m_useOSQP = rf.check("use_osqp", yarp::os::Value(false)).asBool();
    m_dumpData = rf.check("dump_data", yarp::os::Value(false)).asBool();
    leftAdaptedStepParameters(0)=0.0;
    leftAdaptedStepParameters(1)=0.0;
    leftAdaptedStepParameters(2)=0.0;
    yarp::os::Bottle& generalOptions = rf.findGroup("GENERAL");
    m_dT = generalOptions.check("sampling_time", yarp::os::Value(0.016)).asDouble();
    std::string name;
    if(!YarpHelper::getStringFromSearchable(generalOptions, "name", name))
    {
        yError() << "[WalkingModule::configure] Unable to get the string from searchable.";
        return false;
    }
    setName(name.c_str());

    m_robotControlHelper = std::make_unique<RobotHelper>();
    yarp::os::Bottle& robotControlHelperOptions = rf.findGroup("ROBOT_CONTROL");
    robotControlHelperOptions.append(generalOptions);
    if(!m_robotControlHelper->configureRobot(robotControlHelperOptions))
    {
        yError() << "[WalkingModule::configure] Unable to configure the robot.";
        return false;
    }

    yarp::os::Bottle& forceTorqueSensorsOptions = rf.findGroup("FT_SENSORS");
    forceTorqueSensorsOptions.append(generalOptions);
    if(!m_robotControlHelper->configureForceTorqueSensors(forceTorqueSensorsOptions))
    {
        yError() << "[WalkingModule::configure] Unable to configure the Force Torque sensors.";
        return false;
    }

    if(!setRobotModel(rf))
    {
        yError() << "[configure] Unable to set the robot model.";
        return false;
    }

    // open RPC port for external command
    std::string rpcPortName = "/" + getName() + "/rpc";
    this->yarp().attachAsServer(this->m_rpcPort);
    if(!m_rpcPort.open(rpcPortName))
    {
        yError() << "[WalkingModule::configure] Could not open" << rpcPortName << " RPC port.";
        return false;
    }

    std::string desiredUnyciclePositionPortName = "/" + getName() + "/goal:i";
    if(!m_desiredUnyciclePositionPort.open(desiredUnyciclePositionPortName))
    {
        yError() << "[WalkingModule::configure] Could not open" << desiredUnyciclePositionPortName << " port.";
        return false;
    }

    // initialize the trajectory planner
    m_trajectoryGenerator = std::make_unique<TrajectoryGenerator>();
    yarp::os::Bottle& trajectoryPlannerOptions = rf.findGroup("TRAJECTORY_PLANNER");
    trajectoryPlannerOptions.append(generalOptions);
    if(!m_trajectoryGenerator->initialize(trajectoryPlannerOptions))
    {
        yError() << "[configure] Unable to initialize the planner.";
        return false;
    }
    m_stepHeight = trajectoryPlannerOptions.check("stepHeight", yarp::os::Value(0.005)).asDouble();

    if(m_useStepAdaptation)
    {
        // initialize the step adaptation
        m_stepAdaptator = std::make_unique<StepAdaptator>();
        yarp::os::Bottle& stepAdaptatorOptions = rf.findGroup("STEP_ADAPTATOR");
        stepAdaptatorOptions.append(generalOptions);
        if(!m_stepAdaptator->initialize(stepAdaptatorOptions))
        {
            yError() << "[configure] Unable to initialize the step adaptator!";
            return false;
        }
        // yarp::os::Bottle& plannerParameters = rf.findGroup("STEP_ADAPTATOR");
    }


    if(m_useMPC)
    {
        // initialize the MPC controller
        m_walkingController = std::make_unique<WalkingController>();
        yarp::os::Bottle& dcmControllerOptions = rf.findGroup("DCM_MPC_CONTROLLER");
        dcmControllerOptions.append(generalOptions);
        if(!m_walkingController->initialize(dcmControllerOptions))
        {
            yError() << "[WalkingModule::configure] Unable to initialize the controller.";
            return false;
        }
    }
    else
    {
        // initialize the MPC controller
        m_walkingDCMReactiveController = std::make_unique<WalkingDCMReactiveController>();
        yarp::os::Bottle& dcmControllerOptions = rf.findGroup("DCM_REACTIVE_CONTROLLER");
        dcmControllerOptions.append(generalOptions);
        if(!m_walkingDCMReactiveController->initialize(dcmControllerOptions))
        {
            yError() << "[WalkingModule::configure] Unable to initialize the controller.";
            return false;
        }
    }

    // initialize the ZMP controller
    m_walkingZMPController = std::make_unique<WalkingZMPController>();
    yarp::os::Bottle& zmpControllerOptions = rf.findGroup("ZMP_CONTROLLER");
    zmpControllerOptions.append(generalOptions);
    if(!m_walkingZMPController->initialize(zmpControllerOptions))
    {
        yError() << "[WalkingModule::configure] Unable to initialize the ZMP controller.";
        return false;
    }

    // initialize the inverse kinematics solver
    m_IKSolver = std::make_unique<WalkingIK>();
    yarp::os::Bottle& inverseKinematicsSolverOptions = rf.findGroup("INVERSE_KINEMATICS_SOLVER");
    if(!m_IKSolver->initialize(inverseKinematicsSolverOptions, m_loader.model(),
                               m_robotControlHelper->getAxesList()))
    {
        yError() << "[WalkingModule::configure] Failed to configure the ik solver";
        return false;
    }

    if(m_useQPIK)
    {
        yarp::os::Bottle& inverseKinematicsQPSolverOptions = rf.findGroup("INVERSE_KINEMATICS_QP_SOLVER");
        inverseKinematicsQPSolverOptions.append(generalOptions);
        if(m_useOSQP)
            m_QPIKSolver = std::make_unique<WalkingQPIK_osqp>();
        else
            m_QPIKSolver = std::make_unique<WalkingQPIK_qpOASES>();

        if(!m_QPIKSolver->initialize(inverseKinematicsQPSolverOptions,
                                     m_robotControlHelper->getActuatedDoFs(),
                                     m_robotControlHelper->getVelocityLimits(),
                                     m_robotControlHelper->getPositionUpperLimits(),
                                     m_robotControlHelper->getPositionLowerLimits()))
        {
            yError() << "[WalkingModule::configure] Failed to configure the QP-IK solver (qpOASES)";
            return false;
        }
    }

    // initialize the forward kinematics solver
    m_FKSolver = std::make_unique<WalkingFK>();
    yarp::os::Bottle& forwardKinematicsSolverOptions = rf.findGroup("FORWARD_KINEMATICS_SOLVER");
    forwardKinematicsSolverOptions.append(generalOptions);
    if(!m_FKSolver->initialize(forwardKinematicsSolverOptions, m_loader.model()))
    {
        yError() << "[WalkingModule::configure] Failed to configure the fk solver";
        return false;
    }

    // initialize the linear inverted pendulum model
    m_stableDCMModel = std::make_unique<StableDCMModel>();
    if(!m_stableDCMModel->initialize(generalOptions))
    {
        yError() << "[WalkingModule::configure] Failed to configure the lipm.";
        return false;
    }

    // set PIDs gains
    yarp::os::Bottle& pidOptions = rf.findGroup("PID");
    if (!m_robotControlHelper->configurePIDHandler(pidOptions))
    {
        yError() << "[WalkingModule::configure] Failed to configure the PIDs.";
        return false;
    }

    // configure the retargeting
    yarp::os::Bottle retargetingOptions = rf.findGroup("RETARGETING");
    retargetingOptions.append(generalOptions);
    m_retargetingClient = std::make_unique<RetargetingClient>();
    if (!m_retargetingClient->initialize(retargetingOptions, getName(), m_dT))
    {
        yError() << "[WalkingModule::configure] Failed to configure the retargeting";
        return false;
    }

    yarp::os::Bottle contactWrenchMappingOption = rf.findGroup("CONTACT_WRENCH_MAPPING");
    m_contactWrenchMapping = std::make_unique<ContactWrenchMapping>();
    if(!m_contactWrenchMapping->initialize(contactWrenchMappingOption))
    {
        yError() << "[WalkingModule::configure] Failed to configure the contact wrench mapping";
        return false;
    }
    if(!m_contactWrenchMapping->setRobotMass(m_loader.model().getTotalMass()))
        return false;

    yarp::os::Bottle walkingAdmittanceControllerOption = rf.findGroup("ADMITTANCE_CONTROLLER");
    m_walkingAdmittanceController = std::make_unique<WalkingAdmittanceController>();
    if(!m_walkingAdmittanceController->initialize(walkingAdmittanceControllerOption, m_robotControlHelper->getActuatedDoFs()))
    {
        yError() << "[WalkingModule::configure] Failed to configure the walking admittance controller";
        return false;
    }

    // initialize the logger
    if(m_dumpData)
    {
        m_walkingLogger = std::make_unique<LoggerClient>();
        yarp::os::Bottle& loggerOptions = rf.findGroup("WALKING_LOGGER");
        if(!m_walkingLogger->configure(loggerOptions, getName()))
        {
            yError() << "[WalkingModule::configure] Unable to configure the logger.";
            return false;
        }
    }

    // time profiler
    m_profiler = std::make_unique<TimeProfiler>();
    m_profiler->setPeriod(round(0.1 / m_dT));
    if(m_useMPC)
        m_profiler->addTimer("MPC");

    m_profiler->addTimer("IK");
    m_profiler->addTimer("CONTACT_WRENCH");
    m_profiler->addTimer("ADMITTANCE_CONTROLLER");
    m_profiler->addTimer("Total");

    // initialize some variables
    m_newTrajectoryRequired = false;
    m_newTrajectoryMergeCounter = -1;
    m_robotState = WalkingFSM::Configured;

    m_inertial_R_worldFrame = iDynTree::Rotation::Identity();

    // resize variables
    m_qDesired.resize(m_robotControlHelper->getActuatedDoFs());
    m_dqDesired.resize(m_robotControlHelper->getActuatedDoFs());

    yInfo() << "[WalkingModule::configure] Ready to play!";

    return true;
}

void WalkingModule::reset()
{
    if(m_useMPC)
        m_walkingController->reset();

    if(m_useStepAdaptation)
        m_stepAdaptator->reset();

    m_trajectoryGenerator->reset();

    //    if(m_dumpData)
    //        m_walkingLogger->quit();
}

bool WalkingModule::close()
{
    if(m_dumpData)
        m_walkingLogger->quit();

    // restore PID
    m_robotControlHelper->getPIDHandler().restorePIDs();

    // close retargeting ports
    m_retargetingClient->close();

    // close the ports
    m_rpcPort.close();
    m_desiredUnyciclePositionPort.close();

    // close the connection with robot
    if(!m_robotControlHelper->close())
    {
        yError() << "[WalkingModule::close] Unable to close the connection with the robot.";
        return false;
    }

    // clear all the pointer
    m_trajectoryGenerator.reset(nullptr);
    m_walkingController.reset(nullptr);
    m_stepAdaptator.reset(nullptr);
    m_walkingZMPController.reset(nullptr);
    m_IKSolver.reset(nullptr);
    m_QPIKSolver.reset(nullptr);
    m_FKSolver.reset(nullptr);
    m_stableDCMModel.reset(nullptr);

    return true;
}

bool WalkingModule::evaluateContactWrenchDistribution()
{
    m_contactWrenchMapping->setFeetState(m_leftInContact.front(), m_rightInContact.front());

    if(!m_contactWrenchMapping->setCentroidalMomentum(m_FKSolver->getCentroidalTotalMomentum()))
    {
        yError() << "[WalkingModule::evaluateContactWrenchDistribution] Unable to set the centroidal momentum";
        return false;
    }

    iDynTree::Vector3 angularMomentumrateOfChange;
    iDynTree::MatrixDynSize centroidalMomentumJacobian(6, m_robotControlHelper->getActuatedDoFs() + 6);
    m_FKSolver->getCentroidalMomentumJacobian(centroidalMomentumJacobian);
    iDynTree::toEigen(angularMomentumrateOfChange) = (iDynTree::toEigen(centroidalMomentumJacobian) * iDynTree::toEigen(m_walkingAdmittanceController->desiredRobotAcceleration())).tail(3);

    if(!m_contactWrenchMapping->setDesiredAngularMomentumRateOfChange(angularMomentumrateOfChange))
    {
        yError() << "[WalkingModule::evaluateContactWrenchDistribution] Unable to set the angular momentum";
        return false;
    }

    m_contactWrenchMapping->setFeetState(m_FKSolver->getLeftFootToWorldTransform(), m_FKSolver->getRightFootToWorldTransform());

    if(!m_contactWrenchMapping->setCoMState(m_FKSolver->getCoMPosition(), m_FKSolver->getCoMVelocity()))
    {
        yError() << "[WalkingModule::evaluateContactWrenchDistribution] Unable to set the center of mass position and velocity";
        return false;
    }

    if(!m_contactWrenchMapping->setDesiredVRP(m_walkingDCMReactiveController->getControllerOutput()))
    {
        yError() << "[WalkingModule::evaluateContactWrenchDistribution] Unable to set the desired VRP";
        return false;
    }

    if(!m_contactWrenchMapping->setFeetWeightPercentage(m_weightInLeft.front(), m_weightInRight.front()))
    {
        yError() << "[WalkingModule::evaluateContactWrenchDistribution] Unable to set the weight percentage.";
        return false;
    }


    return m_contactWrenchMapping->solve();
}

bool WalkingModule::evaluateAdmittanceControl(const iDynTree::Rotation& desiredNeckOrientation)
{
    m_walkingAdmittanceController->setJointState(m_robotControlHelper->getJointPosition(),
                                                 m_robotControlHelper->getJointVelocity());
    iDynTree::MatrixDynSize massMatrix(m_robotControlHelper->getActuatedDoFs() + 6,
                                       m_robotControlHelper->getActuatedDoFs() + 6);

    iDynTree::VectorDynSize generalizedBiasForces(m_robotControlHelper->getActuatedDoFs() + 6);

    bool ok = true;
    ok &= m_FKSolver->getFreeFloatingMassMatrix(massMatrix);
    m_walkingAdmittanceController->setMassMatrix(massMatrix);

    ok &= m_FKSolver->getGeneralizedBiasForces(generalizedBiasForces);
    m_walkingAdmittanceController->setGeneralizedBiasForces(generalizedBiasForces);

    iDynTree::MatrixDynSize leftFootJacobian(6, m_robotControlHelper->getActuatedDoFs() + 6);
    iDynTree::MatrixDynSize rightFootJacobian(6, m_robotControlHelper->getActuatedDoFs() + 6);

    ok &= m_FKSolver->getLeftFootJacobian(leftFootJacobian);
    ok &= m_FKSolver->getRightFootJacobian(rightFootJacobian);

    m_walkingAdmittanceController->setFeetJacobian(leftFootJacobian, rightFootJacobian);

    m_walkingAdmittanceController->setFeetBiasAcceleration(m_FKSolver->getLeftFootBiasAcceleration(),
                                                           m_FKSolver->getRightFootBiasAcceleration());

    ok &= m_walkingAdmittanceController->setFeetState(m_FKSolver->getLeftFootToWorldTransform(),
                                                      m_FKSolver->getLeftFootVelocity(),
                                                      m_robotControlHelper->getLeftWrench(),
                                                      m_leftInContact.front(),
                                                      m_FKSolver->getRightFootToWorldTransform(),
                                                      m_FKSolver->getRightFootVelocity(),
                                                      m_robotControlHelper->getRightWrench(),
                                                      m_rightInContact.front());

    // TODO add acceleration
    iDynTree::Vector6 dummy;
    dummy.zero();

    ok &= m_walkingAdmittanceController->setDesiredFeetTrajectory(m_leftTrajectory.front(),
                                                                  m_leftTwistTrajectory.front(),
                                                                  m_leftAccelerationTrajectory.front(),
                                                                  m_contactWrenchMapping->getDesiredLeftWrench(),
                                                                  m_rightTrajectory.front(),
                                                                  m_rightTwistTrajectory.front(),
                                                                  m_rightAccelerationTrajectory.front(),
                                                                  m_contactWrenchMapping->getDesiredRightWrench());


    // set neck quantities
    ok &= m_walkingAdmittanceController->setDesiredNeckTrajectory(desiredNeckOrientation.inverse());
    ok &= m_walkingAdmittanceController->setNeckState(m_FKSolver->getNeckOrientation(),
                                                      m_FKSolver->getNeckVelocity());

    iDynTree::MatrixDynSize neckJacobian(6, m_robotControlHelper->getActuatedDoFs() + 6);
    ok &= m_FKSolver->getNeckJacobian(neckJacobian);
    m_walkingAdmittanceController->setNeckJacobian(neckJacobian);
    m_walkingAdmittanceController->setNeckBiasAcceleration(m_FKSolver->getNeckBiasAcceleration());

    iDynTree::MatrixDynSize comJacobian(3, m_robotControlHelper->getActuatedDoFs() + 6);
    ok &= m_walkingAdmittanceController->setDesiredCoMTrajectory(m_FKSolver->getCoMPosition(), m_walkingDCMReactiveController->getControllerOutput());
    ok &= m_FKSolver->getCoMJacobian(comJacobian);
    m_walkingAdmittanceController->setCoMJacobian(comJacobian);
    m_walkingAdmittanceController->setCoMBiasAcceleration(m_FKSolver->getCoMBiasAcceleration());

    if(!ok)
    {
        yError() << "[WalkingModule::evaluateAdmittanceControl] Unable to update parameters in the admittance controller.";
        return false;
    }

    return m_walkingAdmittanceController->solve();
}

bool WalkingModule::solveQPIK(const std::unique_ptr<WalkingQPIK>& solver, const iDynTree::Position& desiredCoMPosition,
                              const iDynTree::Vector3& desiredCoMVelocity,
                              const iDynTree::Rotation& desiredNeckOrientation,
                              iDynTree::VectorDynSize &output)
{
    bool ok = true;
    double threshold = 0.001;
    bool stancePhase = iDynTree::toEigen(m_DCMVelocityDesired.front()).norm() < threshold;
    solver->setPhase(stancePhase);

    ok &= solver->setRobotState(m_robotControlHelper->getJointPosition(),
                                m_FKSolver->getLeftFootToWorldTransform(),
                                m_FKSolver->getRightFootToWorldTransform(),
                                m_FKSolver->getLeftHandToWorldTransform(),
                                m_FKSolver->getRightHandToWorldTransform(),
                                m_FKSolver->getNeckOrientation(),
                                m_FKSolver->getCoMPosition());

    solver->setDesiredNeckOrientation(desiredNeckOrientation.inverse());
    iDynTree::Position newRightFoot;
    iDynTree::Position newLeftFoot;
    iDynTree::LinVelocity newRightFootVel;
    iDynTree::LinVelocity newLeftFootVel;

    newLeftFoot(0)=m_currentFootLeftTransform.getPosition()(0);
    newLeftFoot(1)=m_leftTrajectory.front().getPosition()(1);
    newLeftFoot(2)=m_currentFootLeftTransform.getPosition()(2);

    newRightFoot(0)=m_currentFootRightTransform.getPosition()(0);
    newRightFoot(1)=m_rightTrajectory.front().getPosition()(1);
    newRightFoot(2)=m_currentFootRightTransform.getPosition()(2);

    newLeftFootVel(0)=m_currentFootLeftTwist.getLinearVec3()(0);
    newLeftFootVel(1)=m_leftTwistTrajectory.front().getLinearVec3()(1);
    newLeftFootVel(2)=m_currentFootLeftTwist.getLinearVec3()(2);

    newRightFootVel(0)=m_currentFootRightTwist.getLinearVec3()(0);
    newRightFootVel(1)=m_rightTwistTrajectory.front().getLinearVec3()(1);
    newRightFootVel(2)=m_currentFootRightTwist.getLinearVec3()(2);



//    m_leftTrajectory.front().setPosition(newLeftFoot);
//    m_leftTwistTrajectory.front().setLinearVec3(newLeftFootVel);

//    m_rightTrajectory.front().setPosition(newRightFoot);
//    m_rightTwistTrajectory.front().setLinearVec3(newRightFootVel);


    solver->setDesiredFeetTransformation(m_currentFootLeftTransform,
                                         m_currentFootRightTransform);

    solver->setDesiredFeetTwist(m_currentFootLeftTwist,
                                m_currentFootRightTwist);

    // solver->setDesiredFeetTransformation(m_leftTrajectory.front(),
    //                                      m_rightTrajectory.front());

    // solver->setDesiredFeetTwist(m_leftTwistTrajectory.front(),
    //                             m_rightTwistTrajectory.front());


    solver->setDesiredCoMVelocity(desiredCoMVelocity);
    solver->setDesiredCoMPosition(desiredCoMPosition);

    // TODO probably the problem can be written locally w.r.t. the root or the base
    solver->setDesiredHandsTransformation(m_FKSolver->getHeadToWorldTransform() * m_retargetingClient->leftHandTransform(),
                                          m_FKSolver->getHeadToWorldTransform() * m_retargetingClient->rightHandTransform());

    // set jacobians
    iDynTree::MatrixDynSize jacobian, comJacobian;
    jacobian.resize(6, m_robotControlHelper->getActuatedDoFs() + 6);
    comJacobian.resize(3, m_robotControlHelper->getActuatedDoFs() + 6);

    ok &= m_FKSolver->getLeftFootJacobian(jacobian);
    ok &= solver->setLeftFootJacobian(jacobian);

    ok &= m_FKSolver->getRightFootJacobian(jacobian);
    ok &= solver->setRightFootJacobian(jacobian);

    ok &= m_FKSolver->getNeckJacobian(jacobian);
    ok &= solver->setNeckJacobian(jacobian);

    ok &= m_FKSolver->getCoMJacobian(comJacobian);
    solver->setCoMJacobian(comJacobian);

    ok &= m_FKSolver->getLeftHandJacobian(jacobian);
    ok &= solver->setLeftHandJacobian(jacobian);

    ok &= m_FKSolver->getRightHandJacobian(jacobian);
    ok &= solver->setRightHandJacobian(jacobian);

    if(!ok)
    {
        yError() << "[WalkingModule::solveQPIK] Error while setting the jacobians.";
        return false;
    }

    if(!solver->solve())
    {
        yError() << "[WalkingModule::solveQPIK] Unable to solve the QP-IK problem.";
        return false;
    }

    output = solver->getDesiredJointVelocities();

    return true;
}

bool WalkingModule::updateModule()
{
    std::lock_guard<std::mutex> guard(m_mutex);

    if(m_robotState == WalkingFSM::Preparing)
    {
        if(!m_robotControlHelper->getFeedbacksRaw(10))
        {
            yError() << "[updateModule] Unable to get the feedback.";
            return false;
        }


        bool motionDone = false;
        if(!m_robotControlHelper->checkMotionDone(motionDone))
        {
            yError() << "[WalkingModule::updateModule] Unable to check if the motion is done";
            yInfo() << "[WalkingModule::updateModule] Try to prepare again";
            reset();
            m_robotState = WalkingFSM::Stopped;
            return true;
        }
        if(motionDone)
        {
            // // send the reference again in order to reduce error
            // if(!m_robotControlHelper->setDirectPositionReferences(m_qDesired))
            // {
            //     yError() << "[prepareRobot] Error while setting the initial position using "
            //              << "POSITION DIRECT mode.";
            //     yInfo() << "[WalkingModule::updateModule] Try to prepare again";
            //     reset();
            //     m_robotState = WalkingFSM::Stopped;
            //     return true;
            // }

            yarp::sig::Vector buffer(m_qDesired.size());
            iDynTree::toYarp(m_qDesired, buffer);
            // instantiate Integrator object

            yarp::sig::Matrix jointLimits(m_robotControlHelper->getActuatedDoFs(), 2);
            for(int i = 0; i < m_robotControlHelper->getActuatedDoFs(); i++)
            {
                jointLimits(i, 0) = m_robotControlHelper->getPositionLowerLimits()(i);
                jointLimits(i, 1) = m_robotControlHelper->getPositionUpperLimits()(i);
            }
            m_velocityIntegral = std::make_unique<iCub::ctrl::Integrator>(m_dT, buffer, jointLimits);

            // reset the models
            m_walkingZMPController->reset(m_DCMPositionDesired.front());
            m_stableDCMModel->reset(m_DCMPositionDesired.front());

            // reset the retargeting
            m_retargetingClient->reset(m_FKSolver->getHeadToWorldTransform().inverse()
                                       * m_FKSolver->getLeftHandToWorldTransform(),
                                       m_FKSolver->getHeadToWorldTransform().inverse()
                                       * m_FKSolver->getRightHandToWorldTransform());


            iDynTree::VectorDynSize dummy(m_robotControlHelper->getActuatedDoFs());
            dummy.zero();
            m_walkingAdmittanceController->setDesiredJointTrajectory( m_qDesired, dummy, dummy);

            m_robotState = WalkingFSM::Prepared;
            yInfo() << "[WalkingModule::updateModule] The robot is prepared.";
        }
    }
    else if(m_robotState == WalkingFSM::Walking)
    {

        //  indexmilad=indexmilad+1;
        iDynTree::Vector2 measuredZMP;
        bool resetTrajectory = false;

        m_profiler->setInitTime("Total");

        // check desired planner input
        yarp::sig::Vector* desiredUnicyclePosition = nullptr;
        desiredUnicyclePosition = m_desiredUnyciclePositionPort.read(false);
        if(desiredUnicyclePosition != nullptr)
            if(!setPlannerInput((*desiredUnicyclePosition)(0), (*desiredUnicyclePosition)(1)))
            {
                yError() << "[WalkingModule::updateModule] Unable to set the planner input";
                return false;
            }


        if (m_mergePoints.front() == 21 && desiredUnicyclePosition == nullptr) {

            if(!setPlannerInput(m_desiredPosition(0) ,m_desiredPosition(1)))
            {
                yError() << "[updateModule] Unable to recall the setplannerInput (when terminal (SetGoal) instead of JoyStick is used)";
                return false;
            }
        }


        // if a new trajectory is required check if its the time to evaluate the new trajectory or
        // the time to attach new one
        if(m_newTrajectoryRequired)
        {
            // when we are near to the merge point the new trajectory is evaluated
            if(m_newTrajectoryMergeCounter == 10)
            {
                double initTimeTrajectory;

                initTimeTrajectory = m_time + m_newTrajectoryMergeCounter * m_dT;
                m_startOfWalkingTime=initTimeTrajectory;
                iDynTree::Transform TempRightFoot;
                iDynTree::Transform TempLeftFoot;


                iDynTree::Transform measuredTransform = m_isLeftFixedFrame.front() ?
                    m_rightTrajectory[m_newTrajectoryMergeCounter] :
                    m_leftTrajectory[m_newTrajectoryMergeCounter];

                // ask for a new trajectory
                if(!askNewTrajectories(initTimeTrajectory, !m_isLeftFixedFrame.front(),
                                       measuredTransform, m_newTrajectoryMergeCounter,
                                       m_desiredPosition))
                {
                    yError() << "[WalkingModule::updateModule] Unable to ask for a new trajectory.";
                    return false;
                }
            }


            if (m_newTrajectoryMergeCounter <=10) {
                if(!m_isLeftFixedFrame.front() ){
                yInfo()<<"leftIsNotFixed";
                yInfo()<<m_FKSolver->getLeftFootToWorldTransform().getPosition()(0);
                yInfo()<<m_leftTrajectory[m_newTrajectoryMergeCounter].getPosition()(0);
                yInfo()<<m_currentFootLeftTransform.getPosition()(0);
                yInfo()<<m_newTrajectoryMergeCounter;

                }
            }
            if(m_newTrajectoryMergeCounter == 2)
            {
                if(!updateTrajectories(m_newTrajectoryMergeCounter))
                {
                    yError() << "[WalkingModule::updateModule] Error while updating trajectories. They were not computed yet.";
                    return false;
                }

                //indexmilad=0;
                m_newTrajectoryRequired = false;
                resetTrajectory = true;
            }

            m_newTrajectoryMergeCounter--;
        }



        if (m_robotControlHelper->getPIDHandler().usingGainScheduling())
        {
            if (!m_robotControlHelper->getPIDHandler().updatePhases(m_leftInContact, m_rightInContact, m_time))
            {
                yError() << "[WalkingModule::updateModule] Unable to get the update PID.";
                return false;
            }
        }

        // get feedbacks and evaluate useful quantities
        if(!m_robotControlHelper->getFeedbacks(20))
        {
            yError() << "[WalkingModule::updateModule] Unable to get the feedback.";
            return false;
        }

        m_retargetingClient->getFeedback();

        if(!updateFKSolver())
        {
            yError() << "[WalkingModule::updateModule] Unable to update the FK solver.";
            return false;
        }

        if(!evaluateZMP(measuredZMP))
        {
            yError() << "[WalkingModule::updateModule] Unable to evaluate the ZMP.";
            return false;
        }

        iDynTree::Vector2 mildds= m_DCMPositionDesired.at((m_mergePoints.front()));

        // evaluate 3D-LIPM reference signal
        m_stableDCMModel->setInput(m_DCMPositionDesired.front());
        if(!m_stableDCMModel->integrateModel())
        {
            yError() << "[WalkingModule::updateModule] Unable to propagate the 3D-LIPM.";
            return false;
        }


        // step adjustment
        double comHeight;
        double omega;

        if(!m_trajectoryGenerator->getNominalCoMHeight(comHeight)){
            yError() << "[updateModule] Unable to get the nominal CoM height!";
            return false;
        }

        omega = sqrt(9.81 / comHeight);

        if (!m_leftInContact.front() || !m_rightInContact.front())
        {
            int numberOfSubTrajectories = m_DCMSubTrajectories.size();
            auto firstSS = m_DCMSubTrajectories[numberOfSubTrajectories-2];
            auto secondSS = m_DCMSubTrajectories[numberOfSubTrajectories-4];

            auto secondDS = m_DCMSubTrajectories[numberOfSubTrajectories-3];
            auto firstDS = m_DCMSubTrajectories[numberOfSubTrajectories-1];

            iDynTree::Vector2 nextZmpPosition, currentZmpPosition;
            bool checkFeasibility = false;
            secondSS->getZMPPosition(0, nextZmpPosition, checkFeasibility);
            m_stepAdaptator->setNominalNextStepPosition(nextZmpPosition);

            firstSS->getZMPPosition(0, currentZmpPosition, checkFeasibility);
            m_stepAdaptator->setCurrentZmpPosition(currentZmpPosition);

            // TODO this is a test
            iDynTree::Vector2 dcmCurrentDesired;
            if(!m_DCMSubTrajectories[numberOfSubTrajectories-2]->getDCMPosition(m_time - timeOffset, dcmCurrentDesired, false))
            {
                yError() << " strange " << m_DCMSubTrajectories[numberOfSubTrajectories - 2]->getTrajectoryDomain().first << " " << m_DCMSubTrajectories[numberOfSubTrajectories - 2]->getTrajectoryDomain().second;
                return false;
            }

            m_stepAdaptator->setCurrentDcmPosition(dcmCurrentDesired);

            iDynTree::Vector2 dcmAtTimeAlpha;
            double timeAlpha = (secondDS->getTrajectoryDomain().second + secondDS->getTrajectoryDomain().first) / 2;
            m_DCMSubTrajectories[numberOfSubTrajectories-2]->getDCMPosition(timeAlpha, dcmAtTimeAlpha, checkFeasibility);

            iDynTree::Vector2 nominalDcmOffset;
            iDynTree::toEigen(nominalDcmOffset) = iDynTree::toEigen(dcmAtTimeAlpha) - iDynTree::toEigen(nextZmpPosition);
            m_stepAdaptator->setNominalDcmOffset(nominalDcmOffset);

            m_stepAdaptator->setTimings(omega, m_time - timeOffset, firstSS->getTrajectoryDomain().second,
                                        secondDS->getTrajectoryDomain().second - secondDS->getTrajectoryDomain().first);


            if(!m_stepAdaptator->solve())
            {
                yError() << "unable to solve the problem step adjustment";
                return false;
            }

            impactTimeNominal = firstSS->getTrajectoryDomain().second + timeOffset;
            impactTimeAdjusted = m_stepAdaptator->getDesiredImpactTime() + timeOffset;

            zmpNominal = nextZmpPosition;
            zmpAdjusted = m_stepAdaptator->getDesiredZmp();

            if (!m_leftInContact.front())
            {
                // TODO REMOVE MAGIC NUMBERS
                iDynTree::Vector2 zmpOffset;
                zmpOffset.zero();
                zmpOffset(0) = 0.03;

                m_currentFootLeftTransform = m_adaptatedFootLeftTransform;
                m_currentFootLeftTwist = m_adaptatedFootLeftTwist;
                if(!m_stepAdaptator->getAdaptatedFootTrajectory(m_stepHeight, m_dT, firstSS->getTrajectoryDomain().first,
                                                                m_jLeftstepList.at(1).angle,
                                                                zmpOffset, m_currentFootLeftTransform, m_currentFootLeftTwist,
                                                                m_adaptatedFootLeftTransform, m_adaptatedFootLeftTwist ))
                {
                    yError() << "error write something usefull";
                    return false;
                }
            }
            else
            {
                // TODO REMOVE MAGIC NUMBERS
                iDynTree::Vector2 zmpOffset;
                zmpOffset.zero();
                zmpOffset(0) = 0.03;

                m_currentFootRightTransform = m_adaptatedFootRightTransform;
                m_currentFootRightTwist = m_adaptatedFootRightTwist;
                if(!m_stepAdaptator->getAdaptatedFootTrajectory(m_stepHeight, m_dT, firstSS->getTrajectoryDomain().first,
                                                                m_jRightstepList.at(1).angle,
                                                                zmpOffset, m_currentFootRightTransform, m_currentFootRightTwist,
                                                                m_adaptatedFootRightTransform, m_adaptatedFootRightTwist ))
                {
                    yError() << "error write something usefull right";
                    return false;
                }
            }
        }

        else
        {
            m_currentFootLeftTwist=m_adaptatedFootLeftTwist;
            m_currentFootLeftTransform=m_adaptatedFootLeftTransform;

            m_currentFootRightTwist=m_adaptatedFootRightTwist;
            m_currentFootRightTransform=m_adaptatedFootRightTransform;
        }

        // DCM controller
        if(m_useMPC)
        {
            // Model predictive controller
            m_profiler->setInitTime("MPC");
            if(!m_walkingController->setConvexHullConstraint(m_leftTrajectory, m_rightTrajectory,
                                                             m_leftInContact, m_rightInContact))
            {
                yError() << "[WalkingModule::updateModule] unable to evaluate the convex hull.";
                return false;
            }

            iDynTree::Vector2 dcm2D;
            dcm2D(0) = m_FKSolver->getDCM()(0);
            dcm2D(1) = m_FKSolver->getDCM()(1);
            if(!m_walkingController->setFeedback(dcm2D))
            {
                yError() << "[WalkingModule::updateModule] unable to set the feedback.";
                return false;
            }

            if(!m_walkingController->setReferenceSignal(m_DCMPositionDesired, resetTrajectory))
            {
                yError() << "[WalkingModule::updateModule] unable to set the reference Signal.";
                return false;
            }

            if(!m_walkingController->solve())
            {
                yError() << "[WalkingModule::updateModule] Unable to solve the problem.";
                return false;
            }

            m_profiler->setEndTime("MPC");
        }
        else
        {
            iDynTree::Vector3 DCMPositionDesired3D;
            DCMPositionDesired3D(0) = m_DCMPositionDesired.front()(0);
            DCMPositionDesired3D(1) = m_DCMPositionDesired.front()(1);
            DCMPositionDesired3D(2) = m_comHeightTrajectory.front();

            iDynTree::Vector3 DCMVelocityDesired3D;
            DCMVelocityDesired3D(0) = m_DCMVelocityDesired.front()(0);
            DCMVelocityDesired3D(1) = m_DCMVelocityDesired.front()(1);
            DCMVelocityDesired3D(2) = m_comHeightVelocity.front();

            m_walkingDCMReactiveController->setFeedback(m_FKSolver->getDCM());
            m_walkingDCMReactiveController->setReferenceSignal(DCMPositionDesired3D,
                                                               DCMVelocityDesired3D);

            if(!m_walkingDCMReactiveController->evaluateControl())
            {
                yError() << "[WalkingModule::updateModule] Unable to evaluate the DCM control output.";
                return false;
            }
        }

        // inner COM-ZMP controller
        // if the the norm of desired DCM velocity is lower than a threshold then the robot
        // is stopped
        double threshold = 0.001;
        bool stancePhase = iDynTree::toEigen(m_DCMVelocityDesired.front()).norm() < threshold;
        m_walkingZMPController->setPhase(stancePhase);

        iDynTree::Vector2 desiredZMP;
        if(m_useMPC)
            desiredZMP = m_walkingController->getControllerOutput();
        else
            iDynTree::toEigen(desiredZMP) = iDynTree::toEigen(m_walkingDCMReactiveController->getControllerOutput()).segment(0, 2);

        // set feedback and the desired signal
        m_walkingZMPController->setFeedback(measuredZMP, m_FKSolver->getCoMPosition());
        m_walkingZMPController->setReferenceSignal(desiredZMP, m_stableDCMModel->getCoMPosition(),
                                                   m_stableDCMModel->getCoMVelocity());

        if(!m_walkingZMPController->evaluateControl())
        {
            yError() << "[WalkingModule::updateModule] Unable to evaluate the ZMP control output.";
            return false;
        }

        iDynTree::Vector2 outputZMPCoMControllerPosition, outputZMPCoMControllerVelocity;
        if(!m_walkingZMPController->getControllerOutput(outputZMPCoMControllerPosition,
                                                        outputZMPCoMControllerVelocity))
        {
            yError() << "[WalkingModule::updateModule] Unable to get the ZMP controller output.";
            return false;
        }

        // inverse kinematics
        m_profiler->setInitTime("IK");

        iDynTree::Position desiredCoMPosition;
        desiredCoMPosition(0) = outputZMPCoMControllerPosition(0);
        desiredCoMPosition(1) = outputZMPCoMControllerPosition(1);
        desiredCoMPosition(2) = m_comHeightTrajectory.front();


        iDynTree::Vector3 desiredCoMVelocity;
        desiredCoMVelocity(0) = outputZMPCoMControllerVelocity(0);
        desiredCoMVelocity(1) = outputZMPCoMControllerVelocity(1);
        desiredCoMVelocity(2) = m_comHeightVelocity.front();

        // evaluate desired neck transformation
        double yawLeft = m_leftTrajectory.front().getRotation().asRPY()(2);
        double yawRight = m_rightTrajectory.front().getRotation().asRPY()(2);

        double meanYaw = std::atan2(std::sin(yawLeft) + std::sin(yawRight),
                                    std::cos(yawLeft) + std::cos(yawRight));
        iDynTree::Rotation yawRotation, modifiedInertial;

        yawRotation = iDynTree::Rotation::RotZ(meanYaw);
        yawRotation = yawRotation.inverse();
        modifiedInertial = yawRotation * m_inertial_R_worldFrame;

        if(m_useQPIK)
        {
            // integrate dq because velocity control mode seems not available
            yarp::sig::Vector bufferVelocity(m_robotControlHelper->getActuatedDoFs());
            yarp::sig::Vector bufferPosition(m_robotControlHelper->getActuatedDoFs());

            if(!m_FKSolver->setInternalRobotState(m_qDesired, m_dqDesired))
            {
                yError() << "[WalkingModule::updateModule] Unable to set the internal robot state.";
                return false;
            }

            if(!solveQPIK(m_QPIKSolver, desiredCoMPosition,
                          desiredCoMVelocity,
                          yawRotation, m_dqDesired))
            {
                yError() << "[WalkingModule::updateModule] Unable to solve the QP problem with osqp.";
                return false;
            }

            iDynTree::toYarp(m_dqDesired, bufferVelocity);

            bufferPosition = m_velocityIntegral->integrate(bufferVelocity);
            iDynTree::toiDynTree(bufferPosition, m_qDesired);

            if(!m_FKSolver->setInternalRobotState(m_robotControlHelper->getJointPosition(),
                                                  m_robotControlHelper->getJointVelocity()))
            {
                yError() << "[WalkingModule::updateModule] Unable to set the internal robot state.";
                return false;
            }

        }
        else
        {
            if(m_IKSolver->usingAdditionalRotationTarget())
            {
                if(!m_IKSolver->updateIntertiaToWorldFrameRotation(modifiedInertial))
                {
                    yError() << "[WalkingModule::updateModule] Error updating the inertia to world frame rotation.";
                    return false;
                }

                if(!m_IKSolver->setFullModelFeedBack(m_robotControlHelper->getJointPosition()))
                {
                    yError() << "[WalkingModule::updateModule] Error while setting the feedback to the inverse Kinematics.";
                    return false;
                }

                if(!m_IKSolver->computeIK(m_leftTrajectory.front(), m_rightTrajectory.front(),
                                          desiredCoMPosition, m_qDesired))
                {
                    yError() << "[WalkingModule::updateModule] Error during the inverse Kinematics iteration.";
                    return false;
                }
            }
        }
        m_profiler->setEndTime("IK");

        // if(!m_robotControlHelper->setDirectPositionReferences(m_qDesired))
        // {
        //     yError() << "[WalkingModule::updateModule] Error while setting the reference position to iCub.";
        //     return false;
        // }

        m_profiler->setInitTime("ADMITTANCE_CONTROLLER");

        if(!evaluateAdmittanceControl(yawRotation))
        {
            yError() << "[WalkingModule::updateModule] Unable to evaluate the evaluate admittance control.";
            return false;
        }

        m_profiler->setEndTime("ADMITTANCE_CONTROLLER");

        m_profiler->setInitTime("CONTACT_WRENCH");

        if(!evaluateContactWrenchDistribution())
        {
            yError() << "[WalkingModule::updateModule] Unable to evaluate the contact wrench distribution.";
            return false;
        }

        m_profiler->setEndTime("CONTACT_WRENCH");


        // yarp::sig::Vector bufferAcceleration(m_robotControlHelper->getActuatedDoFs());
        // yarp::sig::Vector bufferVelocity(m_robotControlHelper->getActuatedDoFs());
        // yarp::sig::Vector bufferPosition(m_robotControlHelper->getActuatedDoFs());
        // iDynTree::toYarp(m_walkingAdmittanceController->desiredJointAcceleration(), bufferAcceleration);
        // bufferAcceleration = m_accelerationIntegral->integrate(bufferAcceleration);
        // bufferPosition = m_velocityIntegral->integrate(bufferVelocity);
        // iDynTree::toiDynTree(bufferPosition, m_qDesired);

        // if(!m_robotControlHelper->setDirectPositionReferences(m_qDesired))
        // {
        //     yError() << "[WalkingModule::updateModule] Error while setting the reference position to iCub.";
        //     return false;
        // }

        iDynTree::MatrixDynSize massMatrix(m_robotControlHelper->getActuatedDoFs() + 6,
                                           m_robotControlHelper->getActuatedDoFs() + 6);

        iDynTree::VectorDynSize generalizedBiasForces(m_robotControlHelper->getActuatedDoFs() + 6);

        bool ok = true;
        ok &= m_FKSolver->getFreeFloatingMassMatrix(massMatrix);


        ok &= m_FKSolver->getGeneralizedBiasForces(generalizedBiasForces);


        iDynTree::MatrixDynSize leftFootJacobian(6, m_robotControlHelper->getActuatedDoFs() + 6);
        iDynTree::MatrixDynSize rightFootJacobian(6, m_robotControlHelper->getActuatedDoFs() + 6);

        ok &= m_FKSolver->getLeftFootJacobian(leftFootJacobian);
        ok &= m_FKSolver->getRightFootJacobian(rightFootJacobian);


        iDynTree::VectorDynSize desiredJointTorque(m_robotControlHelper->getActuatedDoFs());
        iDynTree::toEigen(desiredJointTorque) = (iDynTree::toEigen(massMatrix) *
                                                 iDynTree::toEigen(m_walkingAdmittanceController->desiredRobotAcceleration())
                                                 + iDynTree::toEigen(generalizedBiasForces)
                                                 - iDynTree::toEigen(leftFootJacobian).transpose()
                                                 * iDynTree::toEigen(m_contactWrenchMapping->getDesiredLeftWrench())
                                                 - iDynTree::toEigen(rightFootJacobian).transpose()
                                                 * iDynTree::toEigen(m_contactWrenchMapping->getDesiredRightWrench())).tail(m_robotControlHelper->getActuatedDoFs());


        if(!m_robotControlHelper->setTorqueReferences(desiredJointTorque))
        {
            yError() << "[WalkingModule::updateModule] Error while setting the reference torque to iCub.";
            return false;
        }


        m_profiler->setEndTime("Total");

        // print timings
        m_profiler->profiling();

        iDynTree::VectorDynSize errorL(6), errorR(6);
        if(m_useQPIK)
        {
            errorR = m_QPIKSolver->getRightFootError();
            errorL = m_QPIKSolver->getLeftFootError();
        }

        // send data to the WalkingLogger
        if(m_dumpData)
        {
            // iDynTree::Vector2 desiredZMP;
            // if(m_useMPC)
            //     desiredZMP = m_walkingController->getControllerOutput();
            // else
            //     desiredZMP = m_walkingDCMReactiveController->getControllerOutput();

            auto leftFoot = m_FKSolver->getLeftFootToWorldTransform();
            auto rightFoot = m_FKSolver->getRightFootToWorldTransform();

            m_walkingLogger->sendData(m_FKSolver->getDCM(), m_DCMPositionDesired.front(), m_DCMVelocityDesired.front(),
                                      measuredZMP, desiredZMP, m_FKSolver->getCoMPosition(),
                                      m_stableDCMModel->getCoMPosition(),
                                      m_stableDCMModel->getCoMVelocity(),
                                      leftFoot.getPosition(), leftFoot.getRotation().asRPY(),
                                      rightFoot.getPosition(), rightFoot.getRotation().asRPY(),
                                      m_leftTrajectory.front().getPosition(), m_leftTrajectory.front().getRotation().asRPY(),
                                      m_rightTrajectory.front().getPosition(), m_rightTrajectory.front().getRotation().asRPY(),
                                      errorL, errorR);
        }

        propagateTime();

        // advance all the signals
        advanceReferenceSignals();

        m_retargetingClient->setRobotBaseOrientation(yawRotation.inverse());
    }
    return true;
}

bool WalkingModule::evaluateZMP(iDynTree::Vector2& zmp)
{
    if(m_FKSolver == nullptr)
    {
        yError() << "[evaluateZMP] The FK solver is not ready.";
        return false;
    }

    iDynTree::Position zmpLeft, zmpRight, zmpWorld;
    zmpLeft.zero();
    zmpRight.zero();
    double zmpLeftDefined = 0.0, zmpRightDefined = 0.0;

    const iDynTree::Wrench& rightWrench = m_robotControlHelper->getRightWrench();
    if(rightWrench.getLinearVec3()(2) < 0.001)
        zmpRightDefined = 0.0;
    else
    {
        zmpRight(0) = -rightWrench.getAngularVec3()(1) / rightWrench.getLinearVec3()(2);
        zmpRight(1) = rightWrench.getAngularVec3()(0) / rightWrench.getLinearVec3()(2);
        zmpRight(2) = 0.0;
        zmpRightDefined = 1.0;
    }

    const iDynTree::Wrench& leftWrench = m_robotControlHelper->getLeftWrench();
    if(leftWrench.getLinearVec3()(2) < 0.001)
        zmpLeftDefined = 0.0;
    else
    {
        zmpLeft(0) = -leftWrench.getAngularVec3()(1) / leftWrench.getLinearVec3()(2);
        zmpLeft(1) = leftWrench.getAngularVec3()(0) / leftWrench.getLinearVec3()(2);
        zmpLeft(2) = 0.0;
        zmpLeftDefined = 1.0;
    }

    double totalZ = rightWrench.getLinearVec3()(2) + leftWrench.getLinearVec3()(2);
    if(totalZ < 0.1)
    {
        yError() << "[evaluateZMP] The total z-component of contact wrenches is too low.";
        return false;
    }

    zmpLeft = m_FKSolver->getLeftFootToWorldTransform() * zmpLeft;
    zmpRight = m_FKSolver->getRightFootToWorldTransform() * zmpRight;

    // the global zmp is given by a weighted average
    iDynTree::toEigen(zmpWorld) = ((leftWrench.getLinearVec3()(2) * zmpLeftDefined) / totalZ)
            * iDynTree::toEigen(zmpLeft) +
            ((rightWrench.getLinearVec3()(2) * zmpRightDefined)/totalZ) * iDynTree::toEigen(zmpRight);

    zmp(0) = zmpWorld(0);
    zmp(1) = zmpWorld(1);

    return true;
}

bool WalkingModule::prepareRobot(bool onTheFly)
{
    if(m_robotState != WalkingFSM::Configured && m_robotState != WalkingFSM::Stopped)
    {
        yError() << "[WalkingModule::prepareRobot] The robot can be prepared only at the "
                 << "beginning or when the controller is stopped.";
        return false;
    }

    // get the current state of the robot
    // this is necessary because the trajectories for the joints, CoM height and neck orientation
    // depend on the current state of the robot
    bool getExternalRobotBase = true;
    if(!m_robotControlHelper->getFeedbacksRaw(10, getExternalRobotBase))
    {
        yError() << "[WalkingModule::prepareRobot] Unable to get the feedback.";
        return false;
    }

    if(onTheFly)
    {
        if(!m_FKSolver->setBaseOnTheFly())
        {
            yError() << "[WalkingModule::prepareRobot] Unable to set the onTheFly base.";
            return false;
        }

        if(!m_FKSolver->setInternalRobotState(m_robotControlHelper->getJointPosition(),
                                              m_robotControlHelper->getJointVelocity()))
        {
            yError() << "[WalkingModule::prepareRobot] Unable to set joint state.";
            return false;
        }

        // evaluate the left to right transformation, the inertial frame is on the left foot
        iDynTree::Transform leftToRightTransform = m_FKSolver->getRightFootToWorldTransform();

        // evaluate the first trajectory. The robot does not move!
        if(!generateFirstTrajectories(leftToRightTransform))
        {
            yError() << "[WalkingModule::prepareRobot] Failed to evaluate the first trajectories.";
            return false;
        }
    }
    else
    {
        // evaluate the first trajectory. The robot does not move! So the first trajectory
        if(!generateFirstTrajectories())
        {
            yError() << "[WalkingModule::prepareRobot] Failed to evaluate the first trajectories.";
            return false;
        }
    }

    // reset the gains
    if (m_robotControlHelper->getPIDHandler().usingGainScheduling())
    {
        if (!(m_robotControlHelper->getPIDHandler().reset()))
            return false;
    }

    if(!m_IKSolver->setFullModelFeedBack(m_robotControlHelper->getJointPosition()))
    {
        yError() << "[WalkingModule::prepareRobot] Error while setting the feedback to the IK solver.";
        return false;
    }

    iDynTree::Position desiredCoMPosition;
    desiredCoMPosition(0) = m_DCMPositionDesired.front()(0);
    desiredCoMPosition(1) = m_DCMPositionDesired.front()(1);
    desiredCoMPosition(2) = m_comHeightTrajectory.front();

    if(m_IKSolver->usingAdditionalRotationTarget())
    {
        // get the yow angle of both feet
        double yawLeft = m_leftTrajectory.front().getRotation().asRPY()(2);
        double yawRight = m_rightTrajectory.front().getRotation().asRPY()(2);

        // evaluate the mean of the angles
        double meanYaw = std::atan2(std::sin(yawLeft) + std::sin(yawRight),
                                    std::cos(yawLeft) + std::cos(yawRight));
        iDynTree::Rotation yawRotation, modifiedInertial;

        // it is important to notice that the inertial frames rotate with the robot
        yawRotation = iDynTree::Rotation::RotZ(meanYaw);

        yawRotation = yawRotation.inverse();
        modifiedInertial = yawRotation * m_inertial_R_worldFrame;

        if(!m_IKSolver->updateIntertiaToWorldFrameRotation(modifiedInertial))
        {
            yError() << "[WalkingModule::prepareRobot] Error updating the inertia to world frame rotation.";
            return false;
        }
    }

    if(!m_IKSolver->computeIK(m_leftTrajectory.front(), m_rightTrajectory.front(),
                              desiredCoMPosition, m_qDesired))
    {
        yError() << "[WalkingModule::prepareRobot] Inverse Kinematics failed while computing the initial position.";
        return false;
    }

    if(!m_robotControlHelper->setPositionReferences(m_qDesired, 5.0))
    {
        yError() << "[WalkingModule::prepareRobot] Error while setting the initial position.";
        return false;
    }

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_robotState = WalkingFSM::Preparing;
    }

    return true;
}

bool WalkingModule::generateFirstTrajectories(const iDynTree::Transform &leftToRightTransform)
{
    if(m_trajectoryGenerator == nullptr)
    {
        yError() << "[WalkingModule::generateFirstTrajectories] Unicycle planner not available.";
        return false;
    }

    if(!m_trajectoryGenerator->generateFirstTrajectories(leftToRightTransform))
    {
        yError() << "[WalkingModule::generateFirstTrajectories] Failed while retrieving new trajectories from the unicycle";
        return false;
    }

    if(!updateTrajectories(0))
    {
        yError() << "[WalkingModule::generateFirstTrajectories] Unable to update the trajectory.";
        return false;
    }

    // reset the time
    m_time = 0.0;

    return true;
}

bool WalkingModule::generateFirstTrajectories()
{
    if(m_trajectoryGenerator == nullptr)
    {
        yError() << "[WalkingModule::generateFirstTrajectories] Unicycle planner not available.";
        return false;
    }

    if(m_robotControlHelper->isExternalRobotBaseUsed())
    {
        if(!m_trajectoryGenerator->generateFirstTrajectories(m_robotControlHelper->getBaseTransform().getPosition()))
        {
            yError() << "[WalkingModule::generateFirstTrajectories] Failed while retrieving new trajectories from the unicycle";
            return false;
        }
    }
    else
    {
        if(!m_trajectoryGenerator->generateFirstTrajectories())
        {
            yError() << "[WalkingModule::generateFirstTrajectories] Failed while retrieving new trajectories from the unicycle";
            return false;
        }
    }

    if(!updateTrajectories(0))
    {
        yError() << "[WalkingModule::generateFirstTrajectories] Unable to update the trajectory.";
        return false;
    }

    // reset the time
    m_time = 0.0;

    return true;
}

bool WalkingModule::askNewTrajectories(const double& initTime, const bool& isLeftSwinging,
                                       const iDynTree::Transform& measuredTransform,
                                       const size_t& mergePoint, const iDynTree::Vector2& desiredPosition)
{
    if(m_trajectoryGenerator == nullptr)
    {
        yError() << "[WalkingModule::askNewTrajectories] Unicycle planner not available.";
        return false;
    }

    if(mergePoint >= m_DCMPositionDesired.size())
    {
        yError() << "[WalkingModule::askNewTrajectories] The mergePoint has to be lower than the trajectory size.";
        return false;
    }

    if(!m_trajectoryGenerator->updateTrajectories(initTime, m_DCMPositionDesired[mergePoint],
                                                  m_DCMVelocityDesired[mergePoint], isLeftSwinging,
                                                  measuredTransform, desiredPosition))
    {
        yError() << "[WalkingModule::askNewTrajectories] Unable to update the trajectory.";
        return false;
    }
    return true;
}

bool WalkingModule::updateTrajectories(const size_t& mergePoint)
{
    if(!(m_trajectoryGenerator->isTrajectoryComputed()))
    {
        yError() << "[updateTrajectories] The trajectory is not computed.";
        return false;
    }

    std::vector<iDynTree::Transform> leftTrajectory;
    std::vector<iDynTree::Transform> rightTrajectory;
    std::vector<iDynTree::Twist> leftTwistTrajectory;
    std::vector<iDynTree::Twist> rightTwistTrajectory;
    std::vector<iDynTree::SpatialAcc> leftAccelerationTrajectory;
    std::vector<iDynTree::SpatialAcc> rightAccelerationTrajectory;
    std::vector<iDynTree::Vector2> DCMPositionDesired;
    std::vector<iDynTree::Vector2> ZMPPositionDesired;
    std::vector<iDynTree::Vector2> DCMVelocityDesired;
    std::vector<bool> rightInContact;
    std::vector<bool> leftInContact;
    std::vector<double> comHeightTrajectory;
    std::vector<double> comHeightVelocity;
    std::vector<double> weightInLeft;
    std::vector<double> weightInRight;
    std::vector<size_t> mergePoints;
    std::vector<bool> isLeftFixedFrame;


    timeOffset = m_time + mergePoint * m_dT;


    //m_trajectoryGenerator->
    // get dcm position and velocity
    m_trajectoryGenerator->getDCMPositionTrajectory(DCMPositionDesired);
    m_trajectoryGenerator->getDCMVelocityTrajectory(DCMVelocityDesired);
    m_trajectoryGenerator->getZMPPositionTrajectory(ZMPPositionDesired);

    // get feet trajectories
    m_trajectoryGenerator->getFeetTrajectories(leftTrajectory, rightTrajectory);
    m_trajectoryGenerator->getFeetTwist(leftTwistTrajectory, rightTwistTrajectory);
    m_trajectoryGenerator->getFeetAcceleration(leftAccelerationTrajectory, rightAccelerationTrajectory);
    m_trajectoryGenerator->getFeetStandingPeriods(leftInContact, rightInContact);
    m_trajectoryGenerator->getWhenUseLeftAsFixed(isLeftFixedFrame);

    // get com height trajectory
    m_trajectoryGenerator->getCoMHeightTrajectory(comHeightTrajectory);
    m_trajectoryGenerator->getCoMHeightVelocity(comHeightVelocity);

    // get the weight percentage
    m_trajectoryGenerator->getWeightPercentage(weightInLeft, weightInRight);

    // get merge points
    m_trajectoryGenerator->getMergePoints(mergePoints);

    // append vectors to deques
    StdHelper::appendVectorToDeque(leftTrajectory, m_leftTrajectory, mergePoint);
    StdHelper::appendVectorToDeque(rightTrajectory, m_rightTrajectory, mergePoint);
    StdHelper::appendVectorToDeque(leftTwistTrajectory, m_leftTwistTrajectory, mergePoint);
    StdHelper::appendVectorToDeque(rightTwistTrajectory, m_rightTwistTrajectory, mergePoint);
    StdHelper::appendVectorToDeque(leftAccelerationTrajectory, m_leftAccelerationTrajectory, mergePoint);
    StdHelper::appendVectorToDeque(rightAccelerationTrajectory, m_rightAccelerationTrajectory, mergePoint);
    StdHelper::appendVectorToDeque(isLeftFixedFrame, m_isLeftFixedFrame, mergePoint);

    StdHelper::appendVectorToDeque(DCMPositionDesired, m_DCMPositionDesired, mergePoint);
    StdHelper::appendVectorToDeque(ZMPPositionDesired, m_ZMPPositionDesired, mergePoint);
    StdHelper::appendVectorToDeque(DCMVelocityDesired, m_DCMVelocityDesired, mergePoint);

    StdHelper::appendVectorToDeque(leftInContact, m_leftInContact, mergePoint);
    StdHelper::appendVectorToDeque(rightInContact, m_rightInContact, mergePoint);

    StdHelper::appendVectorToDeque(comHeightTrajectory, m_comHeightTrajectory, mergePoint);
    StdHelper::appendVectorToDeque(comHeightVelocity, m_comHeightVelocity, mergePoint);

    StdHelper::appendVectorToDeque(weightInLeft, m_weightInLeft, mergePoint);
    StdHelper::appendVectorToDeque(weightInRight, m_weightInRight, mergePoint);

    m_mergePoints.assign(mergePoints.begin(), mergePoints.end());
    //    for(int var=0;var<m_mergePoints.size(); var++){

    //        yInfo()<<"merge point"<<m_mergePoints[var];

    //    }
    m_DCMSubTrajectories.clear();
    m_trajectoryGenerator->getDCMSubTrajectory(m_DCMSubTrajectories);


    m_trajectoryGenerator->getLeftFootprint(m_jleftFootprints);
    // StepList jLeftstepList=jleftFootprints->getSteps();
    m_jLeftstepList=m_jleftFootprints->getSteps();


    m_trajectoryGenerator->getLeftFootprint(m_jRightFootprints);
    m_jRightstepList=m_jRightFootprints->getSteps();
    // the first merge point is always equal to 0
    m_mergePoints.pop_front();
    m_mergePoints.size();


    m_adaptatedFootLeftTwist.zero();
    m_adaptatedFootRightTwist.zero();

    m_adaptatedFootLeftTransform = leftTrajectory.front();
    m_adaptatedFootRightTransform = rightTrajectory.front();
    m_adaptatedFootRightTwist.zero();

    return true;
}

bool WalkingModule::updateFKSolver()
{
    if(!m_robotControlHelper->isExternalRobotBaseUsed())
    {
        if(!m_FKSolver->evaluateWorldToBaseTransformation(m_leftTrajectory.front(),
                                                          m_rightTrajectory.front(),
                                                          m_isLeftFixedFrame.front()))
        {
            yError() << "[WalkingModule::updateFKSolver] Unable to evaluate the world to base transformation.";
            return false;
        }
    }
    else
    {
        m_FKSolver->evaluateWorldToBaseTransformation(m_robotControlHelper->getBaseTransform(),
                                                      m_robotControlHelper->getBaseTwist());

    }

    if(!m_FKSolver->setInternalRobotState(m_robotControlHelper->getJointPosition(),
                                          m_robotControlHelper->getJointVelocity()))
    {
        yError() << "[WalkingModule::updateFKSolver] Unable to set the robot state.";
        return false;
    }

    return true;
}

bool WalkingModule::startWalking()
{
    std::lock_guard<std::mutex> guard(m_mutex);

    if(m_robotState != WalkingFSM::Prepared && m_robotState != WalkingFSM::Paused)
    {
        yError() << "[WalkingModule::startWalking] Unable to start walking if the robot is not prepared or paused.";
        return false;
    }

    if(m_dumpData)
    {
        m_walkingLogger->startRecord({"record","dcm_x", "dcm_y", "dcm_z",
                    "dcm_des_x", "dcm_des_y",
                    "lf_force_des_x", "lf_force_des_y", "lf_force_des_z", "lf_torque_des_x", "lf_torque_des_y", "lf_torque_des_z",
                    "rf_force_des_x", "rf_force_des_y", "rf_force_des_z", "rf_torque_des_x", "rf_torque_des_y", "rf_torque_des_z",
                    "lf_force_x", "lf_force_y", "lf_force_z", "lf_torque_x", "lf_torque_y", "lf_torque_z",
                    "rf_force_x", "rf_force_y", "rf_force_z", "rf_torque_x", "rf_torque_y", "rf_torque_z"});
    }

    // if the robot was only prepared the filters has to be reseted
    if(m_robotState == WalkingFSM::Prepared)
    {
        m_robotControlHelper->resetFilters();
        updateFKSolver();

        // TODO this is useful for the simulation
        double heightOffset = (m_FKSolver->getLeftFootToWorldTransform().getPosition()(2)
                               + m_FKSolver->getRightFootToWorldTransform().getPosition()(2)) / 2;

        m_robotControlHelper->setHeightOffset(heightOffset);
    }

    m_robotState = WalkingFSM::Walking;

    return true;
}

bool WalkingModule::setPlannerInput(double x, double y)
{
    // the trajectory was already finished the new trajectory will be attached as soon as possible
    if(m_mergePoints.empty())
    {
        if(!(m_leftInContact.front() && m_rightInContact.front()))
        {
            yError() << "[WalkingModule::setPlannerInput] The trajectory has already finished but the system is not in double support.";
            return false;
        }
        if(m_newTrajectoryRequired)
            return true;

        // Since the evaluation of a new trajectory takes time the new trajectory will be merged after x cycles
        m_newTrajectoryMergeCounter = 10;
    }

    // the trajectory was not finished the new trajectory will be attached at the next merge point
    else
    {
        if(m_mergePoints.front() > 10){
            m_newTrajectoryMergeCounter = m_mergePoints.front();
        }
        else if(m_mergePoints.size() > 1)
        {

            if(m_newTrajectoryRequired)
                return true;

            m_newTrajectoryMergeCounter = m_mergePoints[1];
        }
        else
        {

            if(m_newTrajectoryRequired)
                return true;

            m_newTrajectoryMergeCounter = 10;

        }
    }

    m_desiredPosition(0) = x;
    m_desiredPosition(1) = y;

    m_newTrajectoryRequired = true;

    return true;
}

bool WalkingModule::setGoal(double x, double y)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    if(m_robotState != WalkingFSM::Walking)
        return false;

    return setPlannerInput(x, y);
}

bool WalkingModule::pauseWalking()
{
    std::lock_guard<std::mutex> guard(m_mutex);

    if(m_robotState != WalkingFSM::Walking)
        return false;

    // close the logger
    if(m_dumpData)
        m_walkingLogger->quit();

    m_robotState = WalkingFSM::Paused;
    return true;
}

bool WalkingModule::stopWalking()
{
    std::lock_guard<std::mutex> guard(m_mutex);

    if(m_robotState != WalkingFSM::Walking)
        return false;

    reset();

    m_robotState = WalkingFSM::Stopped;
    return true;
}
