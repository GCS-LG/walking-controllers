/**
 * @file TaskBasedTorqueSolver.cpp
 * @authors Giulio Romualdi <giulio.romualdi@iit.it>
 * @copyright 2018 iCub Facility - Istituto Italiano di Tecnologia
 *            Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 * @date 2018
 */

#include <iDynTree/yarp/YARPConfigurationsLoader.h>
#include <iDynTree/Core/Twist.h>
#include <iDynTree/Core/Wrench.h>
#include <iDynTree/Core/EigenHelpers.h>
#include <iDynTree/Core/EigenSparseHelpers.h>

#include <WalkingTaskBasedTorqueSolver.hpp>
#include <Utils.hpp>

typedef Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> MatrixXd;

bool TaskBasedTorqueSolver::instantiateCoMConstraint(const yarp::os::Searchable& config)
{
    if(config.isNull())
    {
        yInfo() << "[instantiateCoMConstraint] Empty configuration file. The CoM Constraint will not be used";
        m_useCoMConstraint = false;
        return true;
    }
    m_useCoMConstraint = true;

    double kp;
    if(!YarpHelper::getNumberFromSearchable(config, "kp", kp))
    {
        yError() << "[instantiateCoMConstraint] Unable to get proportional gain";
        return false;
    }

    double kd;
    if(!YarpHelper::getNumberFromSearchable(config, "kd", kd))
    {
        yError() << "[instantiateCoMConstraint] Unable to get derivative gain";
        return false;
    }

    m_controlOnlyCoMHeight = config.check("controllOnlyHeight", yarp::os::Value("False")).asBool();

    // resize com quantities
    std::shared_ptr<CartesianConstraint> ptr;
    if(!m_controlOnlyCoMHeight)
    {
        m_comJacobian.resize(3, m_actuatedDOFs + 6);
        m_comBiasAcceleration.resize(3);

        // memory allocation
        ptr = std::make_shared<CartesianConstraint>(CartesianElementType::POSITION);
    }
    else
    {
        m_comJacobian.resize(1, m_actuatedDOFs + 6);
        m_comBiasAcceleration.resize(1);

        // memory allocation
        ptr = std::make_shared<CartesianConstraint>(CartesianElementType::ONE_DIMENSION);
    }

    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 0);

    ptr->positionController()->setGains(kp, kd);
    ptr->setRoboticJacobian(m_comJacobian);
    ptr->setBiasAcceleration(m_comBiasAcceleration);

    m_constraints.insert(std::make_pair("com", ptr));
    m_numberOfConstraints += ptr->getNumberOfConstraints();

    return true;
}

// bool TaskBasedTorqueSolver::instantiateLinearMomentumConstraint(const yarp::os::Searchable& config)
// {
//     if(config.isNull())
//     {
//         yInfo() << "[instantiateLinearMomentumConstraint] Empty configuration file. The linear momentum Constraint will not be used";
//         m_useLinearMomentumConstraint = false;
//         return true;
//     }
//     m_useLinearMomentumConstraint = true;

//     // double kp;
//     // if(!YarpHelper::getNumberFromSearchable(config, "kp", kp))
//     // {
//     //     yError() << "[instantiateCoMConstraint] Unable to get proportional gain";
//     //     return false;
//     // }

//     // double kd;
//     // if(!YarpHelper::getNumberFromSearchable(config, "kd", kd))
//     // {
//     //     yError() << "[instantiateCoMConstraint] Unable to get derivative gain";
//     //     return false;
//     // }

//     // memory allocation
//     std::shared_ptr<LinearMomentumConstraint> ptr;
//     ptr = std::make_shared<LinearMomentumConstraint>();
//     ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 6 + m_actuatedDOFs + m_actuatedDOFs);

//     ptr->controller()->setGains(0, 0);

//     m_constraints.insert(std::make_pair("linear_momentum", ptr));
//     m_numberOfConstraints += ptr->getNumberOfConstraints();

//     return true;
// }

// bool TaskBasedTorqueSolver::instantiateAngularMomentumConstraint(const yarp::os::Searchable& config)
// {
//     if(config.isNull())
//     {
//         yInfo() << "[instantiateAngularMomentumConstraint] Empty configuration file. The angular momentum Constraint will not be used";
//         m_useAngularMomentumConstraint = false;
//         return true;
//     }
//     m_useAngularMomentumConstraint = true;

//     double kp;
//     if(!YarpHelper::getNumberFromSearchable(config, "kp", kp))
//     {
//         yError() << "[instantiateAngularMomentumConstraint] Unable to get proportional gain";
//         return false;
//     }

//     // double kd;
//     // if(!YarpHelper::getNumberFromSearchable(config, "kd", kd))
//     // {
//     //     yError() << "[instantiateCoMConstraint] Unable to get derivative gain";
//     //     return false;
//     // }

//     // memory allocation
//     std::shared_ptr<AngularMomentumConstraint> ptr;
//     ptr = std::make_shared<AngularMomentumConstraint>();
//     ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 6 + m_actuatedDOFs + m_actuatedDOFs);

//     ptr->controller()->setGains(kp, 0);

//     ptr->setCoMPosition(m_comPosition);
//     ptr->setLeftFootToWorldTransform(m_leftFootToWorldTransform);
//     ptr->setRightFootToWorldTransform(m_rightFootToWorldTransform);

//     m_constraints.insert(std::make_pair("angular_momentum", ptr));
//     m_numberOfConstraints += ptr->getNumberOfConstraints();

//     return true;
// }

bool TaskBasedTorqueSolver::instantiateRateOfChangeConstraint(const yarp::os::Searchable& config)
{
    yarp::os::Value tempValue;

    if(config.isNull())
    {
        yError() << "[instantiateRateOfChangeConstraint] Empty configuration for rate of change constraint. This constraint will not take into account";
        return true;
    }

    tempValue = config.find("maximumRateOfChange");
    iDynTree::VectorDynSize maximumRateOfChange(m_actuatedDOFs);
    if(!YarpHelper::yarpListToiDynTreeVectorDynSize(tempValue, maximumRateOfChange))
    {
        yError() << "Initialization failed while reading maximumRateOfChange vector.";
        return false;
    }

    std::shared_ptr<RateOfChangeConstraint> ptr;
    ptr = std::make_shared<RateOfChangeConstraint>(m_actuatedDOFs);
    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, m_actuatedDOFs + 6);

    ptr->setMaximumRateOfChange(maximumRateOfChange);
    ptr->setPreviousValues(m_desiredJointTorque);

    m_constraints.insert(std::make_pair("rate_of_change", ptr));
    m_numberOfConstraints += ptr->getNumberOfConstraints();

    return true;
}

bool TaskBasedTorqueSolver::instantiateNeckSoftConstraint(const yarp::os::Searchable& config)
{
    yarp::os::Value tempValue;
    if(config.isNull())
    {
        yError() << "[instantiateNeckSoftConstraint] Empty configuration neck soft constraint.";
        return false;
    }

    // get the neck weight
    tempValue = config.find("neckWeight");
    iDynTree::VectorDynSize neckWeight(3);
    if(!YarpHelper::yarpListToiDynTreeVectorDynSize(tempValue, neckWeight))
    {
        yError() << "[instantiateNeckSoftConstraint] Initialization failed while reading neckWeight.";
        return false;
    }

    double c0, c1, c2;
    if(!YarpHelper::getNumberFromSearchable(config, "c0", c0))
        return false;

    if(!YarpHelper::getNumberFromSearchable(config, "c1", c1))
        return false;

    if(!YarpHelper::getNumberFromSearchable(config, "c2", c2))
        return false;

    if(!iDynTree::parseRotationMatrix(config, "additional_rotation", m_additionalRotation))
    {
        yError() << "[instantiateNeckSoftConstraint] Unable to set the additional rotation.";
        return false;
    }

    m_neckBiasAcceleration.resize(3);
    m_neckJacobian.resize(3, m_actuatedDOFs + 6);

    std::shared_ptr<CartesianCostFunction> ptr;
    ptr = std::make_shared<CartesianCostFunction>(CartesianElementType::ORIENTATION);
    ptr->setSubMatricesStartingPosition(0, 0);

    ptr->setWeight(neckWeight);
    ptr->setBiasAcceleration(m_neckBiasAcceleration);
    ptr->setRoboticJacobian(m_neckJacobian);
    ptr->orientationController()->setGains(c0, c1, c2);

    // resize useful matrix
    m_neckHessian.resize(m_numberOfVariables, m_numberOfVariables);
    m_neckGradient = MatrixXd::Zero(m_numberOfVariables, 1);

    m_costFunction.insert(std::make_pair("neck", ptr));
    m_hessianMatrices.insert(std::make_pair("neck", &m_neckHessian));
    m_gradientVectors.insert(std::make_pair("neck", &m_neckGradient));

    return true;
}

bool TaskBasedTorqueSolver::instantiateRegularizationTaskConstraint(const yarp::os::Searchable& config)
{
    yarp::os::Value tempValue;

    if(config.isNull())
    {
        yError() << "[instantiateRegularizationTaskConstraint] Empty configuration joint task constraint.";
        return false;
    }

    m_jointRegularizationGradient = MatrixXd::Zero(m_numberOfVariables, 1);
    m_jointRegularizationHessian.resize(m_numberOfVariables, m_numberOfVariables);

    m_desiredJointPosition.resize(m_actuatedDOFs);
    m_desiredJointVelocity.resize(m_actuatedDOFs);
    m_desiredJointAcceleration.resize(m_actuatedDOFs);
    m_desiredJointVelocity.zero();
    m_desiredJointAcceleration.zero();

    tempValue = config.find("jointRegularization");
    if(!YarpHelper::yarpListToiDynTreeVectorDynSize(tempValue, m_desiredJointPosition))
    {
        yError() << "[initialize] Unable to convert a YARP list to an iDynTree::VectorDynSize, "
                 << "joint regularization";
        return false;
    }

    iDynTree::toEigen(m_desiredJointPosition) = iDynTree::toEigen(m_desiredJointPosition) *
        iDynTree::deg2rad(1);

    // set the matrix related to the joint regularization
    tempValue = config.find("jointRegularizationWeights");
    iDynTree::VectorDynSize jointRegularizationWeights(m_actuatedDOFs);
    if(!YarpHelper::yarpListToiDynTreeVectorDynSize(tempValue, jointRegularizationWeights))
    {
        yError() << "Initialization failed while reading jointRegularizationWeights vector.";
        return false;
    }

    // set the matrix related to the joint regularization
    tempValue = config.find("proportionalGains");
    iDynTree::VectorDynSize proportionalGains(m_actuatedDOFs);
    if(!YarpHelper::yarpListToiDynTreeVectorDynSize(tempValue, proportionalGains))
    {
        yError() << "Initialization failed while reading proportionalGains vector.";
        return false;
    }

    tempValue = config.find("derivativeGains");
    iDynTree::VectorDynSize derivativeGains(m_actuatedDOFs);
    if(!YarpHelper::yarpListToiDynTreeVectorDynSize(tempValue, derivativeGains))
    {
        yError() << "Initialization failed while reading dxerivativeGains vector.";
        return false;
    }

    std::shared_ptr<JointRegularizationTerm> ptr;
    ptr = std::make_shared<JointRegularizationTerm>(m_actuatedDOFs);

    ptr->setSubMatricesStartingPosition(6, 0);

    ptr->setWeight(jointRegularizationWeights);
    ptr->setDerivativeGains(derivativeGains);
    ptr->setProportionalGains(proportionalGains);

    ptr->setDesiredJointPosition(m_desiredJointPosition);
    ptr->setDesiredJointVelocity(m_desiredJointVelocity);
    ptr->setDesiredJointAcceleration(m_desiredJointAcceleration);
    ptr->setJointPosition(m_jointPosition);
    ptr->setJointVelocity(m_jointVelocity);

    m_costFunction.insert(std::make_pair("regularization_joint", ptr));
    m_hessianMatrices.insert(std::make_pair("regularization_joint", &m_jointRegularizationHessian));
    m_gradientVectors.insert(std::make_pair("regularization_joint", &m_jointRegularizationGradient));


    return true;
}

bool TaskBasedTorqueSolver::instantiateTorqueRegularizationConstraint(const yarp::os::Searchable& config)
{
    yarp::os::Value tempValue;

    if(config.isNull())
    {
        yError() << "[instantiateRegularizationTaskConstraint] Empty configuration torque constraint.";
        return false;
    }

    tempValue = config.find("regularizationWeights");
    iDynTree::VectorDynSize torqueRegularizationWeights(m_actuatedDOFs);
    if(!YarpHelper::yarpListToiDynTreeVectorDynSize(tempValue, torqueRegularizationWeights))
    {
        yError() << "Initialization failed while reading torqueRegularizationWeights vector.";
        return false;
    }

    std::shared_ptr<InputRegularizationTerm> ptr;
    ptr = std::make_shared<InputRegularizationTerm>(m_actuatedDOFs);
    ptr->setSubMatricesStartingPosition(6 + m_actuatedDOFs, 0);
    ptr->setWeight(torqueRegularizationWeights);

    m_torqueRegularizationHessian.resize(m_numberOfVariables, m_numberOfVariables);
    m_torqueRegularizationGradient = MatrixXd::Zero(m_numberOfVariables, 1);

    m_costFunction.insert(std::make_pair("regularization_torque", ptr));
    m_hessianMatrices.insert(std::make_pair("regularization_torque", &m_torqueRegularizationHessian));
    m_gradientVectors.insert(std::make_pair("regularization_torque", &m_torqueRegularizationGradient));
    return true;
}

bool TaskBasedTorqueSolver::initialize(const yarp::os::Searchable& config,
                                       const int& actuatedDOFs,
                                       const iDynTree::VectorDynSize& minJointTorque,
                                       const iDynTree::VectorDynSize& maxJointTorque)
{

    // m_profiler = std::make_unique<TimeProfiler>();
    // m_profiler->setPeriod(round(0.1 / 0.01));

    // m_profiler->addTimer("hessian");
    // m_profiler->addTimer("gradient");
    // m_profiler->addTimer("A");
    // m_profiler->addTimer("add A");
    // m_profiler->addTimer("bounds");
    // m_profiler->addTimer("solve");

    m_actuatedDOFs = actuatedDOFs;
    // depends on single or double support
    setNumberOfVariables();

    m_numberOfConstraints = 0;

    // resize matrices (generic)
    m_massMatrix.resize(m_actuatedDOFs + 6, m_actuatedDOFs + 6);
    m_generalizedBiasForces.resize(m_actuatedDOFs + 6);

    // results
    m_solution.resize(m_numberOfVariables);
    m_desiredJointTorque.resize(m_actuatedDOFs);

    // check if the config is empty
    if(config.isNull())
    {
        yError() << "[initialize] Empty configuration for Task based torque solver.";
        return false;
    }

    // instantiate constraints
    yarp::os::Bottle& comConstraintOptions = config.findGroup("COM");
    if(!instantiateCoMConstraint(comConstraintOptions))
    {
        yError() << "[initialize] Unable to instantiate the CoM constraint.";
        return false;
    }

    // yarp::os::Bottle& linearMomentumConstraintOptions = config.findGroup("LINEAR_MOMENTUM");
    // if(!instantiateLinearMomentumConstraint(linearMomentumConstraintOptions))
    // {
    //     yError() << "[initialize] Unable to instantiate the Linear Momentum constraint.";
    //     return false;
    // }

    // yarp::os::Bottle& angularMomentumConstraintOptions = config.findGroup("ANGULAR_MOMENTUM");
    // if(!instantiateAngularMomentumConstraint(angularMomentumConstraintOptions))
    // {
    //     yError() << "[initialize] Unable to instantiate the Angular Momentum constraint.";
    //     return false;
    // }

    yarp::os::Bottle& feetConstraintOptions = config.findGroup("FEET");
    if(!instantiateFeetConstraint(feetConstraintOptions))
    {
        yError() << "[initialize] Unable to get the instantiate the feet constraints.";
        return false;
    }

    yarp::os::Bottle& ZMPConstraintOptions = config.findGroup("ZMP");
    instantiateZMPConstraint(ZMPConstraintOptions);

    yarp::os::Bottle& contactForcesOption = config.findGroup("CONTACT_FORCES");
    if(!instantiateContactForcesConstraint(contactForcesOption))
    {
        yError() << "[initialize] Unable to get the instantiate the force feet constraints.";
        return false;
    }

    // instantiate cost function
    yarp::os::Bottle& neckOrientationOption = config.findGroup("NECK_ORIENTATION");
    if(!instantiateNeckSoftConstraint(neckOrientationOption))
    {
        yError() << "[initialize] Unable to get the instantiate the neck constraint.";
        return false;
    }

    yarp::os::Bottle& regularizationTaskOption = config.findGroup("REGULARIZATION_TASK");
    if(!instantiateRegularizationTaskConstraint(regularizationTaskOption))
    {
        yError() << "[initialize] Unable to get the instantiate the regularization constraint.";
        return false;
    }

    yarp::os::Bottle& regularizationTorqueOption = config.findGroup("REGULARIZATION_TORQUE");
    if(!instantiateTorqueRegularizationConstraint(regularizationTorqueOption))
    {
        yError() << "[initialize] Unable to get the instantiate the regularization torque constraint.";
        return false;
    }

    yarp::os::Bottle& regularizationForceOption = config.findGroup("REGULARIZATION_FORCE");
    if(!instantiateForceRegularizationConstraint(regularizationForceOption))
    {
        yError() << "[initialize] Unable to get the instantiate the regularization force constraint.";
        return false;
    }

    instantiateSystemDynamicsConstraint();

    yarp::os::Bottle& rateOfChangeOption = config.findGroup("RATE_OF_CHANGE");
    if(!instantiateRateOfChangeConstraint(rateOfChangeOption))
    {
        yError() << "[initialize] Unable to get the instantiate the rate of change constraint.";
        return false;
    }

    // resize
    // sparse matrix
    m_hessianEigen.resize(m_numberOfVariables, m_numberOfVariables);
    m_constraintMatrix.resize(m_numberOfConstraints, m_numberOfVariables);

    // dense vectors
    m_gradient = Eigen::VectorXd::Zero(m_numberOfVariables);
    m_lowerBound = Eigen::VectorXd::Zero(m_numberOfConstraints);
    m_upperBound = Eigen::VectorXd::Zero(m_numberOfConstraints);

    // initialize the optimization problem
    m_optimizer = std::make_unique<OsqpEigen::Solver>();
    m_optimizer->data()->setNumberOfVariables(m_numberOfVariables);
    m_optimizer->data()->setNumberOfConstraints(m_numberOfConstraints);

    m_optimizer->settings()->setVerbosity(false);
    m_optimizer->settings()->setLinearSystemSolver(0);

    // print some usefull information
    yInfo() << "Total number of constraints " << m_numberOfConstraints;
    for(const auto& constraint: m_constraints)
        yInfo() << constraint.first << ": " << constraint.second->getNumberOfConstraints()
                << constraint.second->getJacobianStartingRow()
                << constraint.second->getJacobianStartingColumn();

    return true;
}

bool TaskBasedTorqueSolver::setMassMatrix(const iDynTree::MatrixDynSize& massMatrix)
{
    m_massMatrix = massMatrix;

    if(m_useLinearMomentumConstraint)
    {
        // if first time add robot mass
        if(!m_optimizer->isInitialized())
        {
            auto constraint = m_constraints.find("linear_momentum");
            if(constraint == m_constraints.end())
            {
                yError() << "[setMassMatrix] unable to find the linear constraint. "
                         << "Please call 'initialize()' method";
                return false;
            }

            auto ptr = std::static_pointer_cast<LinearMomentumConstraint>(constraint->second);
            ptr->setRobotMass(m_massMatrix(0,0));
        }
    }

    return true;
}

void TaskBasedTorqueSolver::setGeneralizedBiasForces(const iDynTree::VectorDynSize& generalizedBiasForces)
{
    m_generalizedBiasForces = generalizedBiasForces;
}

// bool TaskBasedTorqueSolver::setLinearAngularMomentum(const iDynTree::SpatialMomentum& linearAngularMomentum)
// {
//     if(m_useAngularMomentumConstraint)
//     {
//         auto constraint = m_constraints.find("angular_momentum");
//         if(constraint == m_constraints.end())
//         {
//             yError() << "[setLinearAngularMomentum] unable to find the linear constraint. "
//                      << "Please call 'initialize()' method";
//             return false;
//         }

//         // set angular momentum
//         iDynTree::Vector3 dummy;
//         dummy.zero();
//         auto ptr = std::static_pointer_cast<AngularMomentumConstraint>(constraint->second);
//         ptr->controller()->setFeedback(dummy, linearAngularMomentum.getAngularVec3());
//     }
//     return true;
// }

void TaskBasedTorqueSolver::setDesiredJointTrajectory(const iDynTree::VectorDynSize& desiredJointPosition,
                                                      const iDynTree::VectorDynSize& desiredJointVelocity,
                                                      const iDynTree::VectorDynSize& desiredJointAcceleration)
{

    m_desiredJointPosition = desiredJointPosition;
    m_desiredJointVelocity = desiredJointVelocity;
    m_desiredJointAcceleration = desiredJointAcceleration;
}

void TaskBasedTorqueSolver::setInternalRobotState(const iDynTree::VectorDynSize& jointPosition,
                                                  const iDynTree::VectorDynSize& jointVelocity)
{
    m_jointPosition = jointPosition;
    m_jointVelocity = jointVelocity;
}

bool TaskBasedTorqueSolver::setDesiredNeckTrajectory(const iDynTree::Rotation& desiredNeckOrientation,
                                                     const iDynTree::Vector3& desiredNeckVelocity,
                                                     const iDynTree::Vector3& desiredNeckAcceleration)
{

    auto cost = m_costFunction.find("neck");
    if(cost == m_costFunction.end())
    {
        yError() << "[setDesiredNeckTrajectory] unable to find the neck trajectory element. "
                 << "Please call 'initialize()' method";
        return false;
    }

    auto ptr = std::static_pointer_cast<CartesianCostFunction>(cost->second);
    ptr->orientationController()->setDesiredTrajectory(desiredNeckAcceleration,
                                                       desiredNeckVelocity,
                                                       desiredNeckOrientation * m_additionalRotation);

    m_desiredNeckOrientation = desiredNeckOrientation * m_additionalRotation;

    return true;
}

bool TaskBasedTorqueSolver::setNeckState(const iDynTree::Rotation& neckOrientation,
                                         const iDynTree::Twist& neckVelocity)
{
    auto cost = m_costFunction.find("neck");
    if(cost == m_costFunction.end())
    {
        yError() << "[setDesiredNeckTrajectory] unable to find the neck trajectory element. "
                 << "Please call 'initialize()' method";
        return false;
    }

    auto ptr = std::static_pointer_cast<CartesianCostFunction>(cost->second);
    ptr->orientationController()->setFeedback(neckVelocity.getAngularVec3(), neckOrientation);

    return true;
}

void TaskBasedTorqueSolver::setNeckJacobian(const iDynTree::MatrixDynSize& jacobian)
{
    iDynTree::toEigen(m_neckJacobian) = iDynTree::toEigen(jacobian).block(3, 0, 3, m_actuatedDOFs +6);
}

void TaskBasedTorqueSolver::setNeckBiasAcceleration(const iDynTree::Vector6 &biasAcceleration)
{
    // get only the angular part
    iDynTree::toEigen(m_neckBiasAcceleration) = iDynTree::toEigen(biasAcceleration).block(3, 0, 3, 1);
}


bool TaskBasedTorqueSolver::setDesiredCoMTrajectory(const iDynTree::Position& comPosition,
                                                    const iDynTree::Vector3& comVelocity,
                                                    const iDynTree::Vector3& comAcceleration)
{
    iDynTree::Vector3 dummy;
    dummy.zero();
    // if(m_useLinearMomentumConstraint)
    // {
    //     std::shared_ptr<LinearMomentumConstraint> ptr;

    //     // save com desired trajectory
    //     auto constraint = m_constraints.find("linear_momentum");
    //     if(constraint == m_constraints.end())
    //     {
    //         yError() << "[setDesiredCoMTrajectory] unable to find the linear momentum constraint. "
    //                  << "Please call 'initialize()' method";
    //         return false;
    //     }

    //     // todo m_massMatrix might be not initialized!!!!!
    //     ptr = std::static_pointer_cast<LinearMomentumConstraint>(constraint->second);
    //     iDynTree::Vector3 desiredLinearMomentumDerivative;
    //     iDynTree::toEigen(desiredLinearMomentumDerivative) = m_massMatrix(0,0) * iDynTree::toEigen(comAcceleration);

    //     ptr->controller()->setDesiredTrajectory(desiredLinearMomentumDerivative, dummy, dummy);
    // }

    if(m_useCoMConstraint)
    {
        auto constraint = m_constraints.find("com");
        if(constraint == m_constraints.end())
        {
            yError() << "[setDesiredCoMTrajectory] unable to find the linear momentum constraint. "
                     << "Please call 'initialize()' method";
            return false;
        }

        auto ptr = std::static_pointer_cast<CartesianConstraint>(constraint->second);
        ptr->positionController()->setDesiredTrajectory(dummy, comVelocity, comPosition);
    }
    return true;
}

bool TaskBasedTorqueSolver::setCoMState(const iDynTree::Position& comPosition,
                                        const iDynTree::Vector3& comVelocity)
{
    if(m_useCoMConstraint)
    {
        auto constraint = m_constraints.find("com");
        if(constraint == m_constraints.end())
        {
            yError() << "[setCoMState] unable to find the right foot constraint. "
                     << "Please call 'initialize()' method";
            return false;
        }
        auto ptr = std::static_pointer_cast<CartesianConstraint>(constraint->second);
        ptr->positionController()->setFeedback(comVelocity, comPosition);
    }
    m_comPosition = comPosition;

    return true;
}

void TaskBasedTorqueSolver::setCoMJacobian(const iDynTree::MatrixDynSize& comJacobian)
{
    if(m_useCoMConstraint)
    {
        if(!m_controlOnlyCoMHeight)
            m_comJacobian = comJacobian;
        else
            iDynTree::toEigen(m_comJacobian) = iDynTree::toEigen(comJacobian).block(2, 0, 1, m_actuatedDOFs + 6);
    }
}

void TaskBasedTorqueSolver::setCoMBiasAcceleration(const iDynTree::Vector3 &comBiasAcceleration)
{
    if(m_useCoMConstraint)
    {
        if(!m_controlOnlyCoMHeight)
            iDynTree::toEigen(m_comBiasAcceleration) = iDynTree::toEigen(comBiasAcceleration);
        else
            m_comBiasAcceleration(0) = comBiasAcceleration(2);
    }
}

bool TaskBasedTorqueSolver::setDesiredZMP(const iDynTree::Vector2 &zmp)
{
    if(m_useZMPConstraint)
    {
        std::shared_ptr<ZMPConstraint> ptr;

        auto constraint = m_constraints.find("zmp");
        if(constraint == m_constraints.end())
        {
            yError() << "[setDesiredZMP] Unable to find the zmp constraint. "
                     << "Please call 'initialize()' method";
            return false;
        }
        ptr = std::static_pointer_cast<ZMPConstraint>(constraint->second);
        ptr->setDesiredZMP(zmp);
    }
    return true;
}

bool TaskBasedTorqueSolverDoubleSupport::setFeetWeightPercentage(const double &weightInLeft,
                                                                 const double &weightInRight)
{
    iDynTree::VectorDynSize weightLeft(6), weightRight(6);

    for(int i = 0; i < 6; i++)
    {
        weightLeft(i) = m_regularizationForceScale * std::fabs(weightInLeft)
            + m_regularizationForceOffset;

        weightRight(i) = m_regularizationForceScale * std::fabs(weightInRight)
            + m_regularizationForceOffset;
    }

    auto cost = m_costFunction.find("regularization_left_force");
    if(cost == m_costFunction.end())
    {
        yError() << "[setDesiredNeckTrajectory] unable to find the neck trajectory element. "
                 << "Please call 'initialize()' method";
        return false;
    }
    auto ptr = std::static_pointer_cast<InputRegularizationTerm>(cost->second);
    ptr->setWeight(weightLeft);

    cost = m_costFunction.find("regularization_right_force");
    if(cost == m_costFunction.end())
    {
        yError() << "[setDesiredNeckTrajectory] unable to find the neck trajectory element. "
                 << "Please call 'initialize()' method";
        return false;
    }
    ptr = std::static_pointer_cast<InputRegularizationTerm>(cost->second);
    ptr->setWeight(weightRight);

    return true;
}

bool TaskBasedTorqueSolver::setHessianMatrix()
{
    std::string key;
    Eigen::SparseMatrix<double> hessianEigen(m_numberOfVariables, m_numberOfVariables);
    for(const auto& element: m_costFunction)
    {
        key = element.first;
        element.second->evaluateHessian(*(m_hessianMatrices.at(key)));
        hessianEigen+= *(m_hessianMatrices.at(key));
    }

    if(m_optimizer->isInitialized())
    {
        if(!m_optimizer->updateHessianMatrix(hessianEigen))
        {
            yError() << "[setHessianMatrix] Unable to update the hessian matrix.";
            return false;
        }
    }
    else
    {
        if(!m_optimizer->data()->setHessianMatrix(hessianEigen))
        {
            yError() << "[setHessianMatrix] Unable to set first time the hessian matrix.";
            return false;
        }
    }

    m_hessianEigen = hessianEigen;

    return true;
}

bool TaskBasedTorqueSolver::setGradientVector()
{
    std::string key;
    Eigen::VectorXd gradientEigen = MatrixXd::Zero(m_numberOfVariables, 1);
    for(const auto& element: m_costFunction)
    {
        key = element.first;
        element.second->evaluateGradient(*(m_gradientVectors.at(key)));
        gradientEigen += *(m_gradientVectors.at(key));
    }

    if(m_optimizer->isInitialized())
    {
        if(!m_optimizer->updateGradient(gradientEigen))
        {
            yError() << "[setGradient] Unable to update the gradient.";
            return false;
        }
    }
    else
    {
        if(!m_optimizer->data()->setGradient(gradientEigen))
        {
            yError() << "[setGradient] Unable to set first time the gradient.";
            return false;
        }
    }

    m_gradient = gradientEigen;

    return true;
}

bool TaskBasedTorqueSolver::setLinearConstraintMatrix()
{
    for(const auto& constraint: m_constraints)
    {
        // m_profiler->setInitTime("add A");
        constraint.second->evaluateJacobian(m_constraintMatrix);
        // m_profiler->setEndTime("add A");
    }

    if(m_optimizer->isInitialized())
    {
        if(!m_optimizer->updateLinearConstraintsMatrix(m_constraintMatrix))
        {
            yError() << "[setLinearConstraintsMatrix] Unable to update the constraints matrix.";
            return false;
        }
    }
    else
    {
        if(!m_optimizer->data()->setLinearConstraintsMatrix(m_constraintMatrix))
        {
            yError() << "[setLinearConstraintsMatrix] Unable to set the constraints matrix.";
            return false;
        }
    }

    return true;
}

bool TaskBasedTorqueSolver::setBounds()
{
    for(const auto& constraint: m_constraints)
        constraint.second->evaluateBounds(m_upperBound, m_lowerBound);

    if(m_optimizer->isInitialized())
    {
        if(!m_optimizer->updateBounds(m_lowerBound, m_upperBound))
        {
            yError() << "[setBounds] Unable to update the bounds.";
            return false;
        }
    }
    else
    {
        if(!m_optimizer->data()->setLowerBound(m_lowerBound))
        {
            yError() << "[setBounds] Unable to set the first time the lower bound.";
            return false;
        }

        if(!m_optimizer->data()->setUpperBound(m_upperBound))
        {
            yError() << "[setBounds] Unable to set the first time the upper bound.";
            return false;
        }
    }
    return true;
}

bool TaskBasedTorqueSolver::solve()
{
    // m_profiler->setInitTime("hessian");

    if(!setHessianMatrix())
    {
        yError() << "[solve] Unable to set the hessian matrix.";
        return false;
    }

    // m_profiler->setEndTime("hessian");

    // m_profiler->setInitTime("gradient");

    if(!setGradientVector())
    {
        yError() << "[solve] Unable to set the gradient vector matrix.";
        return false;
    }

    // m_profiler->setEndTime("gradient");

    // m_profiler->setInitTime("A");

    if(!setLinearConstraintMatrix())
    {
        yError() << "[solve] Unable to set the linear constraint matrix.";
        return false;
    }

    // m_profiler->setEndTime("A");

    // m_profiler->setInitTime("bounds");
    if(!setBounds())
    {
        yError() << "[solve] Unable to set the bounds.";
        return false;
    }

    // m_profiler->setEndTime("bounds");

    // m_profiler->setInitTime("solve");
    if(!m_optimizer->isInitialized())
    {
        if(!m_optimizer->initSolver())
        {
            yError() << "[solve] Unable to initialize the solver";
            return false;
        }
    }

    if(!m_optimizer->solve())
    {
        std::cerr << "hessian = [\n";
        std::cerr<< Eigen::MatrixXd(m_hessianEigen) << "\n";
        std::cerr << "];\n";

        std::cerr << "gradient = [\n";
        std::cerr<< Eigen::MatrixXd(m_gradient) << "\n";
        std::cerr << "];\n";

        std::cerr << "constraint_j = [\n";
        std::cerr<< Eigen::MatrixXd(m_constraintMatrix) << "\n";
        std::cerr << "];\n";

        std::cerr << "lower_bound = [\n";
        std::cerr<< Eigen::MatrixXd(m_lowerBound) << "\n";
        std::cerr << "];\n";

        std::cerr << "upper_bound = [\n";
        std::cerr<< Eigen::MatrixXd(m_upperBound) << "\n";
        std::cerr << "];\n";

        yError() << "[solve] Unable to solve the problem.";
        return false;
    }

    m_solution = m_optimizer->getSolution();

    // check equality constraints
    // if(!isSolutionFeasible())
    // {
    //     yError() << "[solve] The solution is not feasible.";
    //     return false;
    // }

    for(int i = 0; i < m_actuatedDOFs; i++)
        m_desiredJointTorque(i) = m_solution(i + m_actuatedDOFs + 6);

    // Eigen::VectorXd product;
    // auto leftWrench = getLeftWrench();
    // auto rightWrench = getRightWrench();
    // Eigen::VectorXd wrenches(12);
    // wrenches.block(0,0,6,1) = iDynTree::toEigen(leftWrench);
    // wrenches.block(6,0,6,1) = iDynTree::toEigen(rightWrench);

    // auto constraint = m_constraints.find("angular_momentum");
    // if(constraint == m_constraints.end())
    // {
    //     yError() << "[setLinearAngularMomentum] unable to find the linear constraint. "
    //              << "Please call 'initialize()' method";
    //     return false;
    // }


    // yInfo() << "zmp jacobian starting row " << constraint->second->getJacobianStartingRow();
    // yInfo() << "zmp jacobian starting column " << constraint->second->getJacobianStartingColumn();
    // Eigen::MatrixXd JacobianZMP;
    // JacobianZMP = m_constraintMatrix.block(constraint->second->getJacobianStartingRow(),
    //                                             0,
    //                                             3, m_numberOfVariables);


    // product = JacobianZMP * m_solution;

    // std::cerr << "Jacobian \n" << JacobianZMP << "\n";
    // std::cerr << "product \n" << product << "\n";

    // std::cerr << "upper bounds \n"
    //           << m_upperBound.block(constraint->second->getJacobianStartingRow(),
    //                                 0, 3, 1)<< "\n";

    // std::cerr << "lower bounds \n"
    //           << m_lowerBound.block(constraint->second->getJacobianStartingRow(),
    //                                 0, 3, 1)<< "\n";


    // Eigen::VectorXd product;
    // auto leftWrench = getLeftWrench();
    // auto rightWrench = getRightWrench();
    // Eigen::VectorXd wrenches(12);
    // wrenches.block(0,0,6,1) = iDynTree::toEigen(leftWrench);
    // wrenches.block(6,0,6,1) = iDynTree::toEigen(rightWrench);

    // auto constraint = m_constraints.find("zmp");
    // if(constraint == m_constraints.end())
    // {
    //     yError() << "[setLinearAngularMomentum] unable to find the linear constraint. "
    //              << "Please call 'initialize()' method";
    //     return false;
    // }


    // yInfo() << "zmp jacobian starting row " << constraint->second->getJacobianStartingRow();
    // yInfo() << "zmp jacobian starting column " << constraint->second->getJacobianStartingColumn();
    // Eigen::MatrixXd JacobianZMP(2,12);
    // JacobianZMP = m_constraintMatrix.block(constraint->second->getJacobianStartingRow(),
    //                                             constraint->second->getJacobianStartingColumn(),
    //                                             2, 12);


    // product = JacobianZMP * wrenches;

    // std::cerr << "JacaobianZMP \n" << JacobianZMP << "\n";
    // std::cerr << "product \n" << product << "\n";

    // std::cerr << "upper bounds \n"
    //           << m_upperBound.block(constraint->second->getJacobianStartingRow(),
    //                                 0, 2, 1)<< "\n";

    // std::cerr << "lower bounds \n"
    //           << m_lowerBound.block(constraint->second->getJacobianStartingRow(),
    //                                 0, 2, 1)<< "\n";

    // m_profiler->setEndTime("solve");

    // m_profiler->profiling();

    return true;
}

bool TaskBasedTorqueSolver::isSolutionFeasible()
{
    double tolerance = 0.5;
    Eigen::VectorXd constrainedOutput = m_constraintMatrix * m_solution;
    // std::cerr<<"m_constraintMatrix"<<std::endl;
    // std::cerr<<Eigen::MatrixXd(m_constraintMatrix)<<std::endl;

    // std::cerr<<"upper\n";
    // std::cerr<<constrainedOutput - m_upperBound<<std::endl;

    // std::cerr<<"lower\n";
    // std::cerr<<constrainedOutput - m_lowerBound<<std::endl;

    // std::cerr<<"solution\n";
    // std::cerr<<m_solution<<"\n";

    if(((constrainedOutput - m_upperBound).maxCoeff() < tolerance)
       && ((constrainedOutput - m_lowerBound).minCoeff() > -tolerance))
        return true;

    yError() << "[isSolutionFeasible] The constraints are not satisfied.";
    return false;
}

void TaskBasedTorqueSolver::getSolution(iDynTree::VectorDynSize& output)
{
    output = m_desiredJointTorque;
}

iDynTree::Wrench TaskBasedTorqueSolverDoubleSupport::getLeftWrench()
{
    iDynTree::Wrench wrench;
    for(int i = 0; i < 6; i++)
        wrench(i) = m_solution(6 + m_actuatedDOFs + m_actuatedDOFs + i);

    return wrench;
}

iDynTree::Wrench TaskBasedTorqueSolverDoubleSupport::getRightWrench()
{

    iDynTree::Wrench wrench;
    for(int i = 0; i < 6; i++)
        wrench(i) = m_solution(6 + m_actuatedDOFs + m_actuatedDOFs + 6 + i);

    return wrench;
}

iDynTree::Vector3 TaskBasedTorqueSolver::getDesiredNeckOrientation()
{
    return m_desiredNeckOrientation.asRPY();
}

bool TaskBasedTorqueSolverDoubleSupport::instantiateFeetConstraint(const yarp::os::Searchable& config)
{
    if(config.isNull())
    {
        yError() << "[instantiateFeetConstraint] Empty configuration file.";
        return false;
    }

    std::shared_ptr<CartesianConstraint> ptr;

    // left foot
    // resize quantities
    m_leftFootJacobian.resize(6, m_actuatedDOFs + 6);
    m_leftFootBiasAcceleration.resize(6);

    ptr = std::make_shared<CartesianConstraint>(CartesianElementType::CONTACT);
    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 0);
    ptr->setRoboticJacobian(m_leftFootJacobian);
    ptr->setBiasAcceleration(m_leftFootBiasAcceleration);

    m_constraints.insert(std::make_pair("left_foot", ptr));
    m_numberOfConstraints += ptr->getNumberOfConstraints();

    // right foot
    // resize quantities
    m_rightFootJacobian.resize(6, m_actuatedDOFs + 6);
    m_rightFootBiasAcceleration.resize(6);
    ptr = std::make_shared<CartesianConstraint>(CartesianElementType::CONTACT);
    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 0);
    ptr->setRoboticJacobian(m_rightFootJacobian);
    ptr->setBiasAcceleration(m_rightFootBiasAcceleration);

    m_constraints.insert(std::make_pair("right_foot", ptr));
    m_numberOfConstraints += ptr->getNumberOfConstraints();

    return true;
}

void TaskBasedTorqueSolverDoubleSupport::instantiateZMPConstraint(const yarp::os::Searchable& config)
{
    if(config.isNull())
    {
        yInfo() << "[instantiateZMPConstraint] Empty configuration file. The ZMP Constraint will not be used";
        m_useZMPConstraint = false;
        return;
    }
    m_useZMPConstraint = true;

    auto ptr = std::make_shared<ZMPConstraintDoubleSupport>();
    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 6 + m_actuatedDOFs + m_actuatedDOFs);
    ptr->setLeftFootToWorldTransform(m_leftFootToWorldTransform);
    ptr->setRightFootToWorldTransform(m_rightFootToWorldTransform);

    m_constraints.insert(std::make_pair("zmp", ptr));

    m_numberOfConstraints += ptr->getNumberOfConstraints();
}

void TaskBasedTorqueSolverDoubleSupport::instantiateSystemDynamicsConstraint()
{
    auto ptr = std::make_shared<SystemDynamicConstraintDoubleSupport>(m_actuatedDOFs);
    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 0);
    ptr->setLeftFootJacobian(m_leftFootJacobian);
    ptr->setRightFootJacobian(m_rightFootJacobian);
    ptr->setMassMatrix(m_massMatrix);
    ptr->setGeneralizedBiasForces(m_generalizedBiasForces);

    m_constraints.insert(std::make_pair("system_dynamics", ptr));
    m_numberOfConstraints += ptr->getNumberOfConstraints();
}

bool TaskBasedTorqueSolverDoubleSupport::instantiateContactForcesConstraint(const yarp::os::Searchable& config)
{
    if(config.isNull())
    {
        yError() << "[instantiateFeetConstraint] Empty configuration file.";
        return false;
    }

    double staticFrictionCoefficient;
    if(!YarpHelper::getNumberFromSearchable(config, "staticFrictionCoefficient",
                                            staticFrictionCoefficient))
    {
        yError() << "[initialize] Unable to get the number from searchable.";
        return false;
    }

    int numberOfPoints;
    if(!YarpHelper::getNumberFromSearchable(config, "numberOfPoints", numberOfPoints))
    {
        yError() << "[initialize] Unable to get the number from searchable.";
        return false;
    }

    double torsionalFrictionCoefficient;
    if(!YarpHelper::getNumberFromSearchable(config, "torsionalFrictionCoefficient",
                                            torsionalFrictionCoefficient))
    {
        yError() << "[initialize] Unable to get the number from searchable.";
        return false;
    }

    // feet dimensions
    yarp::os::Value feetDimensions = config.find("foot_size");
    if(feetDimensions.isNull() || !feetDimensions.isList())
    {
        yError() << "Please set the foot_size in the configuration file.";
        return false;
    }

    yarp::os::Bottle *feetDimensionsPointer = feetDimensions.asList();
    if(!feetDimensionsPointer || feetDimensionsPointer->size() != 2)
    {
        yError() << "Error while reading the feet dimensions. Wrong number of elements.";
        return false;
    }

    yarp::os::Value& xLimits = feetDimensionsPointer->get(0);
    if(xLimits.isNull() || !xLimits.isList())
    {
        yError() << "Error while reading the X limits.";
        return false;
    }

    yarp::os::Bottle *xLimitsPtr = xLimits.asList();
    if(!xLimitsPtr || xLimitsPtr->size() != 2)
    {
        yError() << "Error while reading the X limits. Wrong dimensions.";
        return false;
    }

    iDynTree::Vector2 footLimitX;
    footLimitX(0) = xLimitsPtr->get(0).asDouble();
    footLimitX(1) = xLimitsPtr->get(1).asDouble();

    yarp::os::Value& yLimits = feetDimensionsPointer->get(1);
    if(yLimits.isNull() || !yLimits.isList())
    {
        yError() << "Error while reading the Y limits.";
        return false;
    }

    yarp::os::Bottle *yLimitsPtr = yLimits.asList();
    if(!yLimitsPtr || yLimitsPtr->size() != 2)
    {
        yError() << "Error while reading the Y limits. Wrong dimensions.";
        return false;
    }

    iDynTree::Vector2 footLimitY;
    footLimitY(0) = yLimitsPtr->get(0).asDouble();
    footLimitY(1) = yLimitsPtr->get(1).asDouble();

    double minimalNormalForce;
    if(!YarpHelper::getNumberFromSearchable(config, "minimalNormalForce", minimalNormalForce))
    {
        yError() << "[initialize] Unable to get the number from searchable.";
        return false;
    }

    std::shared_ptr<ForceConstraint> ptr;

    // left foot
    ptr = std::make_shared<ForceConstraint>(numberOfPoints);
    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 2 * m_actuatedDOFs + 6);

    ptr->setStaticFrictionCoefficient(staticFrictionCoefficient);
    ptr->setTorsionalFrictionCoefficient(torsionalFrictionCoefficient);
    ptr->setMinimalNormalForce(minimalNormalForce);
    ptr->setFootSize(footLimitX, footLimitY);
    ptr->setFootToWorldTransform(m_leftFootToWorldTransform);

    m_constraints.insert(std::make_pair("left_force", ptr));
    m_numberOfConstraints += ptr->getNumberOfConstraints();

    // right foot
    ptr = std::make_shared<ForceConstraint>(numberOfPoints);
    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 2 * m_actuatedDOFs + 6 + 6);

    ptr->setStaticFrictionCoefficient(staticFrictionCoefficient);
    ptr->setTorsionalFrictionCoefficient(torsionalFrictionCoefficient);
    ptr->setMinimalNormalForce(minimalNormalForce);
    ptr->setFootSize(footLimitX, footLimitY);
    ptr->setFootToWorldTransform(m_rightFootToWorldTransform);

    m_constraints.insert(std::make_pair("right_force", ptr));
    m_numberOfConstraints += ptr->getNumberOfConstraints();

    return true;
}

bool TaskBasedTorqueSolverDoubleSupport::instantiateForceRegularizationConstraint(const yarp::os::Searchable& config)
{
    yarp::os::Value tempValue;

    if(config.isNull())
    {
        yError() << "[instantiateRegularizationTaskConstraint] Empty configuration torque constraint.";
        return false;
    }

    if(!YarpHelper::getNumberFromSearchable(config, "regularizationForceScale", m_regularizationForceScale))
    {
        yError() << "[instantiateForceRegularizationConstraint] Unable to get regularization force scale";
        return false;
    }

    if(!YarpHelper::getNumberFromSearchable(config, "regularizationForceOffset", m_regularizationForceOffset))
    {
        yError() << "[instantiateForceRegularizationConstraint] Unable to get regularization force offset";
        return false;
    }

    m_leftForceRegularizationHessian.resize(m_numberOfVariables, m_numberOfVariables);
    m_leftForceRegularizationGradient = MatrixXd::Zero(m_numberOfVariables, 1);

    m_rightForceRegularizationHessian.resize(m_numberOfVariables, m_numberOfVariables);
    m_rightForceRegularizationGradient = MatrixXd::Zero(m_numberOfVariables, 1);

    std::shared_ptr<InputRegularizationTerm> ptr;
    ptr = std::make_shared<InputRegularizationTerm>(6);
    ptr->setSubMatricesStartingPosition(6 + m_actuatedDOFs + m_actuatedDOFs, 0);
    m_costFunction.insert(std::make_pair("regularization_left_force", ptr));
    m_hessianMatrices.insert(std::make_pair("regularization_left_force",
                                            &m_leftForceRegularizationHessian));
    m_gradientVectors.insert(std::make_pair("regularization_left_force",
                                            &m_leftForceRegularizationGradient));

    ptr = std::make_shared<InputRegularizationTerm>(6);
    ptr->setSubMatricesStartingPosition(6 + m_actuatedDOFs + m_actuatedDOFs + 6, 0);
    m_costFunction.insert(std::make_pair("regularization_right_force", ptr));
    m_hessianMatrices.insert(std::make_pair("regularization_right_force",
                                            &m_rightForceRegularizationHessian));
    m_gradientVectors.insert(std::make_pair("regularization_right_force",
                                            &m_rightForceRegularizationGradient));

    return true;
}

void TaskBasedTorqueSolverDoubleSupport::setNumberOfVariables()
{
    // the optimization variable is given by
    // 1. base + joint acceleration (6 + m_actuatedDOFs)
    // 2. joint torque (m_actuatedDOFs)
    // 3. left and right foot contact wrench (6 + 6)

    m_numberOfVariables = 6 + m_actuatedDOFs + m_actuatedDOFs + 6 + 6;
}

void TaskBasedTorqueSolverDoubleSupport::setFeetState(const iDynTree::Transform& leftFootToWorldTransform,
                                                      const iDynTree::Transform& rightFootToWorldTransform)
{
    m_leftFootToWorldTransform = leftFootToWorldTransform;
    m_rightFootToWorldTransform = rightFootToWorldTransform;
}

void TaskBasedTorqueSolverDoubleSupport::setFeetJacobian(const iDynTree::MatrixDynSize& leftFootJacobian,
                                                         const iDynTree::MatrixDynSize& rightFootJacobian)
{
    m_rightFootJacobian = rightFootJacobian;
    m_leftFootJacobian = leftFootJacobian;
}

void TaskBasedTorqueSolverDoubleSupport::setFeetBiasAcceleration(const iDynTree::Vector6 &leftFootBiasAcceleration,
                                                                 const iDynTree::Vector6 &rightFootBiasAcceleration)
{

    iDynTree::toEigen(m_leftFootBiasAcceleration) = iDynTree::toEigen(leftFootBiasAcceleration);
    iDynTree::toEigen(m_rightFootBiasAcceleration) = iDynTree::toEigen(rightFootBiasAcceleration);
}

iDynTree::Vector2 TaskBasedTorqueSolverDoubleSupport::getZMP()
{
    iDynTree::Position zmpLeft, zmpRight, zmpWorld;
    double zmpLeftDefined = 0.0, zmpRightDefined = 0.0;

    iDynTree::Vector2 zmp;

    auto leftWrench = getLeftWrench();
    auto rightWrench = getRightWrench();

    if(rightWrench.getLinearVec3()(2) < 10)
        zmpRightDefined = 0.0;
    else
    {
        zmpRight(0) = -rightWrench.getAngularVec3()(1) / rightWrench.getLinearVec3()(2);
        zmpRight(1) = rightWrench.getAngularVec3()(0) / rightWrench.getLinearVec3()(2);
        zmpRight(2) = 0.0;
        zmpRightDefined = 1.0;
    }

    if(leftWrench.getLinearVec3()(2) < 10)
        zmpLeftDefined = 0.0;
    else
    {
        zmpLeft(0) = -leftWrench.getAngularVec3()(1) / leftWrench.getLinearVec3()(2);
        zmpLeft(1) = leftWrench.getAngularVec3()(0) / leftWrench.getLinearVec3()(2);
        zmpLeft(2) = 0.0;
        zmpLeftDefined = 1.0;
    }

    double totalZ = rightWrench.getLinearVec3()(2) + leftWrench.getLinearVec3()(2);

    iDynTree::Transform leftTrans(iDynTree::Rotation::Identity(), m_leftFootToWorldTransform.getPosition());
    iDynTree::Transform rightTrans(iDynTree::Rotation::Identity(), m_rightFootToWorldTransform.getPosition());
    zmpLeft = leftTrans * zmpLeft;
    zmpRight = rightTrans * zmpRight;

    // the global zmp is given by a weighted average
    iDynTree::toEigen(zmpWorld) = ((leftWrench.getLinearVec3()(2) * zmpLeftDefined) / totalZ)
        * iDynTree::toEigen(zmpLeft) +
        ((rightWrench.getLinearVec3()(2) * zmpRightDefined)/totalZ) * iDynTree::toEigen(zmpRight);

    zmp(0) = zmpWorld(0);
    zmp(1) = zmpWorld(1);

    return zmp;
}

bool TaskBasedTorqueSolverSingleSupport::instantiateFeetConstraint(const yarp::os::Searchable& config)
{
    if(config.isNull())
    {
        yError() << "[instantiateFeetConstraint] Empty configuration file.";
        return false;
    }

    double kp;
    if(!YarpHelper::getNumberFromSearchable(config, "kp", kp))
    {
        yError() << "[instantiateFeetConstraint] Unable to get proportional gain";
        return false;
    }

    double kd;
    if(!YarpHelper::getNumberFromSearchable(config, "kd", kd))
    {
        yError() << "[instantiateFeetConstraint] Unable to get derivative gain";
        return false;
    }

    double c0;
    if(!YarpHelper::getNumberFromSearchable(config, "c0", c0))
    {
        yError() << "[instantiateFeetConstraint] Unable to get c0";
        return false;
    }

    double c1;
    if(!YarpHelper::getNumberFromSearchable(config, "c1", c1))
    {
        yError() << "[instantiateFeetConstraint] Unable to get c1";
        return false;
    }

    double c2;
    if(!YarpHelper::getNumberFromSearchable(config, "c2", c2))
    {
        yError() << "[instantiateFeetConstraint] Unable to get c2";
        return false;
    }

    std::shared_ptr<CartesianConstraint> ptr;

    // stance_foot
    // resize quantities
    m_stanceFootJacobian.resize(6, m_actuatedDOFs + 6);
    m_stanceFootBiasAcceleration.resize(6);

    ptr = std::make_shared<CartesianConstraint>(CartesianElementType::CONTACT);
    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 0);
    ptr->setRoboticJacobian(m_stanceFootJacobian);
    ptr->setBiasAcceleration(m_stanceFootBiasAcceleration);
    m_constraints.insert(std::make_pair("stance_foot", ptr));
    m_numberOfConstraints += ptr->getNumberOfConstraints();

    // swing foot
    // resize quantities
    m_swingFootJacobian.resize(6, m_actuatedDOFs + 6);
    m_swingFootBiasAcceleration.resize(6);

    ptr = std::make_shared<CartesianConstraint>(CartesianElementType::POSE);
    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 0);
    ptr->positionController()->setGains(kp, kd);
    ptr->orientationController()->setGains(c0, c1, c2);
    ptr->setRoboticJacobian(m_swingFootJacobian);
    ptr->setBiasAcceleration(m_swingFootBiasAcceleration);

    m_constraints.insert(std::make_pair("swing_foot", ptr));
    m_numberOfConstraints += ptr->getNumberOfConstraints();

    return true;
}

void TaskBasedTorqueSolverSingleSupport::instantiateZMPConstraint(const yarp::os::Searchable& config)
{
    if(config.isNull())
    {
        yInfo() << "[instantiateZMPConstraint] Empty configuration file. The ZMP Constraint will not be used";
        m_useZMPConstraint = false;
        return;
    }
    m_useZMPConstraint = true;

    auto ptr = std::make_shared<ZMPConstraintSingleSupport>();
    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 6 + m_actuatedDOFs + m_actuatedDOFs);
    ptr->setStanceFootToWorldTransform(m_stanceFootToWorldTransform);

    m_constraints.insert(std::make_pair("zmp", ptr));
    m_numberOfConstraints += ptr->getNumberOfConstraints();
}

void TaskBasedTorqueSolverSingleSupport::instantiateSystemDynamicsConstraint()
{
    auto ptr = std::make_shared<SystemDynamicConstraintSingleSupport>(m_actuatedDOFs);
    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 0);
    ptr->setStanceFootJacobian(m_stanceFootJacobian);
    ptr->setMassMatrix(m_massMatrix);
    ptr->setGeneralizedBiasForces(m_generalizedBiasForces);

    m_constraints.insert(std::make_pair("system_dynamics", ptr));
    m_numberOfConstraints += ptr->getNumberOfConstraints();
}

bool TaskBasedTorqueSolverSingleSupport::instantiateContactForcesConstraint(const yarp::os::Searchable& config)
{
    if(config.isNull())
    {
        yError() << "[instantiateFeetConstraint] Empty configuration file.";
        return false;
    }

    double staticFrictionCoefficient;
    if(!YarpHelper::getNumberFromSearchable(config, "staticFrictionCoefficient",
                                            staticFrictionCoefficient))
    {
        yError() << "[initialize] Unable to get the number from searchable.";
        return false;
    }

    int numberOfPoints;
    if(!YarpHelper::getNumberFromSearchable(config, "numberOfPoints", numberOfPoints))
    {
        yError() << "[initialize] Unable to get the number from searchable.";
        return false;
    }

    double torsionalFrictionCoefficient;
    if(!YarpHelper::getNumberFromSearchable(config, "torsionalFrictionCoefficient",
                                            torsionalFrictionCoefficient))
    {
        yError() << "[initialize] Unable to get the number from searchable.";
        return false;
    }

    // feet dimensions
    yarp::os::Value feetDimensions = config.find("foot_size");
    if(feetDimensions.isNull() || !feetDimensions.isList())
    {
        yError() << "Please set the foot_size in the configuration file.";
        return false;
    }

    yarp::os::Bottle *feetDimensionsPointer = feetDimensions.asList();
    if(!feetDimensionsPointer || feetDimensionsPointer->size() != 2)
    {
        yError() << "Error while reading the feet dimensions. Wrong number of elements.";
        return false;
    }

    yarp::os::Value& xLimits = feetDimensionsPointer->get(0);
    if(xLimits.isNull() || !xLimits.isList())
    {
        yError() << "Error while reading the X limits.";
        return false;
    }

    yarp::os::Bottle *xLimitsPtr = xLimits.asList();
    if(!xLimitsPtr || xLimitsPtr->size() != 2)
    {
        yError() << "Error while reading the X limits. Wrong dimensions.";
        return false;
    }

    iDynTree::Vector2 footLimitX;
    footLimitX(0) = xLimitsPtr->get(0).asDouble();
    footLimitX(1) = xLimitsPtr->get(1).asDouble();

    yarp::os::Value& yLimits = feetDimensionsPointer->get(1);
    if(yLimits.isNull() || !yLimits.isList())
    {
        yError() << "Error while reading the Y limits.";
        return false;
    }

    yarp::os::Bottle *yLimitsPtr = yLimits.asList();
    if(!yLimitsPtr || yLimitsPtr->size() != 2)
    {
        yError() << "Error while reading the Y limits. Wrong dimensions.";
        return false;
    }

    iDynTree::Vector2 footLimitY;
    footLimitY(0) = yLimitsPtr->get(0).asDouble();
    footLimitY(1) = yLimitsPtr->get(1).asDouble();

    double minimalNormalForce;
    if(!YarpHelper::getNumberFromSearchable(config, "minimalNormalForce", minimalNormalForce))
    {
        yError() << "[initialize] Unable to get the number from searchable.";
        return false;
    }

    std::shared_ptr<ForceConstraint> ptr;

    // stance foot
    ptr = std::make_shared<ForceConstraint>(numberOfPoints);
    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 2 * m_actuatedDOFs + 6);

    ptr->setStaticFrictionCoefficient(staticFrictionCoefficient);
    ptr->setTorsionalFrictionCoefficient(torsionalFrictionCoefficient);
    ptr->setMinimalNormalForce(minimalNormalForce);
    ptr->setFootSize(footLimitX, footLimitY);
    ptr->setFootToWorldTransform(m_stanceFootToWorldTransform);

    m_constraints.insert(std::make_pair("stance_force", ptr));
    m_numberOfConstraints += ptr->getNumberOfConstraints();

    return true;
}

bool TaskBasedTorqueSolverSingleSupport::instantiateForceRegularizationConstraint(const yarp::os::Searchable& config)
{
    yarp::os::Value tempValue;

    if(config.isNull())
    {
        yError() << "[instantiateRegularizationTaskConstraint] Empty configuration torque constraint.";
        return false;
    }

    if(!YarpHelper::getNumberFromSearchable(config, "regularizationForceScale", m_regularizationForceScale))
    {
        yError() << "[instantiateForceRegularizationConstraint] Unable to get regularization force scale";
        return false;
    }

    if(!YarpHelper::getNumberFromSearchable(config, "regularizationForceOffset", m_regularizationForceOffset))
    {
        yError() << "[instantiateForceRegularizationConstraint] Unable to get regularization force offset";
        return false;
    }

    m_stanceForceRegularizationHessian.resize(m_numberOfVariables, m_numberOfVariables);
    m_stanceForceRegularizationGradient = MatrixXd::Zero(m_numberOfVariables, 1);

    // the weight is constant in stance phase
    iDynTree::VectorDynSize weight(6);
    for(int i = 0; i < 6; i++)
        weight(i) = m_regularizationForceScale + m_regularizationForceOffset;

    std::shared_ptr<InputRegularizationTerm> ptr;
    ptr = std::make_shared<InputRegularizationTerm>(6);
    ptr->setSubMatricesStartingPosition(6 + m_actuatedDOFs + m_actuatedDOFs, 0);
    ptr->setWeight(weight);

    m_costFunction.insert(std::make_pair("regularization_stance_force", ptr));
    m_hessianMatrices.insert(std::make_pair("regularization_stance_force",
                                            &m_stanceForceRegularizationHessian));
    m_gradientVectors.insert(std::make_pair("regularization_stance_force",
                                            &m_stanceForceRegularizationGradient));

    return true;
}

void TaskBasedTorqueSolverSingleSupport::setNumberOfVariables()
{
    // the optimization variable is given by
    // 1. base + joint acceleration
    // 2. joint torque
    // 3. stance foot contact wrench

    m_numberOfVariables = 6 + m_actuatedDOFs + m_actuatedDOFs + 6;
}

bool TaskBasedTorqueSolverSingleSupport::setDesiredFeetTrajectory(const iDynTree::Transform& swingFootToWorldTransform,
                                                                  const iDynTree::Twist& swingFootTwist,
                                                                  const iDynTree::Twist& swingFootAcceleration)
{
    std::shared_ptr<CartesianConstraint> ptr;

    // save left foot trajectory
    auto constraint = m_constraints.find("swing_foot");
    if(constraint == m_constraints.end())
    {
        yError() << "[setDesiredFeetTrajectory] unable to find the swing foot constraint. "
                 << "Please call 'initialize()' method";
        return false;
    }
    iDynTree::Vector3 dummy;
    dummy.zero();
    ptr = std::static_pointer_cast<CartesianConstraint>(constraint->second);
    ptr->positionController()->setDesiredTrajectory(dummy,
                                                    swingFootTwist.getLinearVec3(),
                                                    swingFootToWorldTransform.getPosition());

    ptr->orientationController()->setDesiredTrajectory(dummy,
                                                       swingFootTwist.getAngularVec3(),
                                                       swingFootToWorldTransform.getRotation());
    return true;
}

bool TaskBasedTorqueSolverSingleSupport::setFeetState(const iDynTree::Transform& stanceFootToWorldTransform,
                                                      const iDynTree::Transform& swingFootToWorldTransform,
                                                      const iDynTree::Twist& swingFootTwist)
{
    m_stanceFootToWorldTransform = stanceFootToWorldTransform;

    std::shared_ptr<CartesianConstraint> ptr;

    // left foot
    auto constraint = m_constraints.find("swing_foot");
    if(constraint == m_constraints.end())
    {
        yError() << "[setFeetState] unable to find the swing foot constraint. "
                 << "Please call 'initialize()' method";
        return false;
    }
    ptr = std::static_pointer_cast<CartesianConstraint>(constraint->second);
    ptr->positionController()->setFeedback(swingFootTwist.getLinearVec3(),
                                           swingFootToWorldTransform.getPosition());

    ptr->orientationController()->setFeedback(swingFootTwist.getAngularVec3(),
                                              swingFootToWorldTransform.getRotation());

    return true;
}

void TaskBasedTorqueSolverSingleSupport::setFeetJacobian(const iDynTree::MatrixDynSize& stanceFootJacobian,
                                                         const iDynTree::MatrixDynSize& swingFootJacobian)
{
    m_stanceFootJacobian = stanceFootJacobian;
    m_swingFootJacobian = swingFootJacobian;
}

void TaskBasedTorqueSolverSingleSupport::setFeetBiasAcceleration(const iDynTree::Vector6 &stanceFootBiasAcceleration,
                                                                 const iDynTree::Vector6 &swingFootBiasAcceleration)
{

    iDynTree::toEigen(m_stanceFootBiasAcceleration) = iDynTree::toEigen(stanceFootBiasAcceleration);
    iDynTree::toEigen(m_swingFootBiasAcceleration) = iDynTree::toEigen(swingFootBiasAcceleration);
}

iDynTree::Wrench TaskBasedTorqueSolverSingleSupport::getStanceWrench()
{
    iDynTree::Wrench wrench;
    for(int i = 0; i < 6; i++)
        wrench(i) = m_solution(6 + m_actuatedDOFs + m_actuatedDOFs + i);

    return wrench;
}

iDynTree::Vector2 TaskBasedTorqueSolverSingleSupport::getZMP()
{
    iDynTree::Position zmpPos;

    iDynTree::Vector2 zmp;

    auto stanceWrench = getStanceWrench();

    zmpPos(0) = -stanceWrench.getAngularVec3()(1) / stanceWrench.getLinearVec3()(2);
    zmpPos(1) = stanceWrench.getAngularVec3()(0) / stanceWrench.getLinearVec3()(2);
    zmpPos(2) = 0.0;

    iDynTree::Transform trans(iDynTree::Rotation::Identity(),
                              m_stanceFootToWorldTransform.getPosition());

    zmpPos = trans * zmpPos;
    zmp(0) = zmpPos(0);
    zmp(1) = zmpPos(1);

    return zmp;
}
