/**
 * @file WalkingQPInverseKinematics_osqp.cpp
 * @authors Giulio Romualdi <giulio.romualdi@iit.it>
 * @copyright 2018 iCub Facility - Istituto Italiano di Tecnologia
 *            Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 * @date 2018
 */

// std
#include <cmath>

// YARP
#include <yarp/os/LogStream.h>
#include <yarp/os/Value.h>

// iDynTree
#include <iDynTree/Core/EigenHelpers.h>
#include <iDynTree/Core/EigenSparseHelpers.h>
#include <iDynTree/Model/Model.h>
#include "iDynTree/yarp/YARPConfigurationsLoader.h"

#include <WalkingQPInverseKinematics_osqp.hpp>

bool WalkingQPIK_osqp::setJointsBounds(const iDynTree::VectorDynSize& maxJointsVelocity,
                                       const iDynTree::VectorDynSize& maxJointsPosition,
                                       const iDynTree::VectorDynSize& minJointsPosition)
{
    if(maxJointsVelocity.size() != maxJointsPosition.size() ||
       maxJointsVelocity.size() != minJointsPosition.size())
    {
        yError() << "[setVelocityBounds] The size of the vector limits has to be equal.";
        return false;
    }
    if(maxJointsVelocity.size() != m_actuatedDOFs)
    {
        yError() << "[setVelocityBounds] The size of the vector limits has to be equal to ."
                 << "the number of the joint";
        return false;
    }
    m_maxJointsVelocity = maxJointsVelocity;
    m_maxJointsPosition = maxJointsPosition;
    m_minJointsPosition = minJointsPosition;
    return true;
}

bool WalkingQPIK_osqp::initialize(const yarp::os::Searchable& config,
                                  const int& actuatedDOFs,
                                  const iDynTree::VectorDynSize& maxJointsVelocity,
                                  const iDynTree::VectorDynSize& maxJointsPosition,
                                  const iDynTree::VectorDynSize& minJointsPosition)
{

    m_actuatedDOFs = actuatedDOFs;
    // check if the config is empty
    if(config.isNull())
    {
        yError() << "[initialize] Empty configuration for QP-IKg solver.";
        return false;
    }

    m_useCoMAsConstraint = config.check("useCoMAsConstraint", yarp::os::Value(false)).asBool();

    // TODO in the future the number of constraints should be added inside
    // the configuration file
    // set the number of variables and the number of constraints
    // the number of variables is equal to the number of joints plus
    // 6 (position + attitude) degree of freedom related to the base.
    m_numberOfVariables = m_actuatedDOFs + 6;

    // the number of constraints is equal to the number of joints plus
    // 12 (position + attitude) of the left and right feet
    if(m_useCoMAsConstraint)
    {
        m_numberOfConstraints = m_actuatedDOFs + 6 + 6 + 3;
        m_numberOfTaskConstraints = 6 + 6 + 3;
    }
    else
    {
        m_numberOfConstraints = m_actuatedDOFs + 6 + 6;
        m_numberOfTaskConstraints = 6 + 6;
    }

    // resize vectors
    m_gradient = Eigen::VectorXd::Zero(m_numberOfVariables);
    m_lowerBound = Eigen::VectorXd::Zero(m_numberOfConstraints);
    m_upperBound = Eigen::VectorXd::Zero(m_numberOfConstraints);

    m_regularizationTerm.resize(m_actuatedDOFs);
    m_jointPosition.resize(m_actuatedDOFs);

    // get the regularization term
    yarp::os::Value jointRegularization = config.find("jointRegularization");
    if(!YarpHelper::yarpListToiDynTreeVectorDynSize(jointRegularization, m_regularizationTerm))
    {
        yError() << "[initialize] Unable to convert a YARP list to an iDynTree::VectorDynSize, "
                 << "joint regularization";
        return false;
    }

    iDynTree::toEigen(m_regularizationTerm) = iDynTree::toEigen(m_regularizationTerm) *
        iDynTree::deg2rad(1);

    // preprare constant matrix necessary for the QP problem
    if(!initializeMatrices(config))
    {
        yError() << "[initialize] Unable to Initialize the constant matrix.";
        return false;
    }

    if(!YarpHelper::getNumberFromSearchable(config, "k_u", m_ku))
        return false;

    if(!YarpHelper::getNumberFromSearchable(config, "k_b", m_kb))
        return false;


    if(!setJointsBounds(maxJointsVelocity, maxJointsPosition, minJointsPosition))
    {
        yError() << "[initialize] Unable to set the velocity bounds.";
        return false;
    }


    if(!iDynTree::parseRotationMatrix(config, "additional_rotation", m_additionalRotation))
    {
        yError() << "[initialize] Unable to set the additional rotation.";
        return false;
    }

    // instantiate the solver
    m_optimizerSolver = std::make_unique<OsqpEigen::Solver>();
    m_optimizerSolver->data()->setNumberOfVariables(m_numberOfVariables);
    m_optimizerSolver->data()->setNumberOfConstraints(m_numberOfConstraints);

    m_optimizerSolver->settings()->setVerbosity(false);
    m_optimizerSolver->settings()->setLinearSystemSolver(0);

    return true;
}

bool WalkingQPIK_osqp::setHessianMatrix()
{
    // evaluate the hessian matrix
    Eigen::SparseMatrix<double> hessianEigenSparse;

    // in that case the hessian matrix is related only to neck orientation and to
    // the joint angle
    m_hessianEigenDense = Eigen::MatrixXd(iDynTree::toEigen(m_jointRegulatizationHessian)) +
        iDynTree::toEigen(m_neckJacobian).transpose() *
        iDynTree::toEigen(m_neckWeightMatrix) *
        iDynTree::toEigen(m_neckJacobian);

    if(!m_useCoMAsConstraint)
    {
        m_hessianEigenDense =  m_hessianEigenDense + iDynTree::toEigen(m_comJacobian).transpose() *
            iDynTree::toEigen(m_comWeightMatrix) * iDynTree::toEigen(m_comJacobian);
    }

    Eigen::SparseMatrix<double> hessianEigen = m_hessianEigenDense.sparseView();

    if(m_optimizerSolver->isInitialized())
    {
        if(!m_optimizerSolver->updateHessianMatrix(hessianEigen))
        {
            yError() << "[setHessianMatrix] Unable to update the hessian matrix.";
            return false;
        }
    }
    else
    {
        if(!m_optimizerSolver->data()->setHessianMatrix(hessianEigen))
        {
            yError() << "[setHessianMatrix] Unable to set first time the hessian matrix.";
            return false;
        }
    }
    return true;
}

bool WalkingQPIK_osqp::setGradientVector()
{
    iDynTree::Matrix3x3 errorNeckAttitude = iDynTreeHelper::Rotation::skewSymmetric(m_neckOrientation * m_desiredNeckOrientation.inverse());

    // todo
    if(m_useCoMAsConstraint)
    {
        m_gradient = -iDynTree::toEigen(m_neckJacobian).transpose()
            * iDynTree::toEigen(m_neckWeightMatrix) *
            m_kAttFoot * (-m_kNeck * iDynTree::unskew(iDynTree::toEigen(errorNeckAttitude)))
            - iDynTree::toEigen(m_jointRegulatizationGradient) *
            (iDynTree::toEigen(m_jointRegulatizationGains) * (iDynTree::toEigen(m_regularizationTerm)
                                                              - iDynTree::toEigen(m_jointPosition)));
    }
    else
    {
        m_gradient = -iDynTree::toEigen(m_comJacobian).transpose()
            * iDynTree::toEigen(m_comWeightMatrix) * iDynTree::toEigen(m_comVelocity)
            - iDynTree::toEigen(m_neckJacobian).transpose() * iDynTree::toEigen(m_neckWeightMatrix) *
            m_kAttFoot * (-m_kNeck * iDynTree::unskew(iDynTree::toEigen(errorNeckAttitude)))
            - iDynTree::toEigen(m_jointRegulatizationGradient) *
            (iDynTree::toEigen(m_jointRegulatizationGains) * (iDynTree::toEigen(m_regularizationTerm)
                                                              - iDynTree::toEigen(m_jointPosition)));
    }

    if(m_optimizerSolver->isInitialized())
    {
        if(!m_optimizerSolver->updateGradient<Eigen::Dynamic>(m_gradient))
        {
            yError() << "[setGradient] Unable to update the gradient.";
            return false;
        }
    }
    else
    {
        if(!m_optimizerSolver->data()->setGradient<Eigen::Dynamic>(m_gradient))
        {
            yError() << "[setGradient] Unable to set first time the gradient.";
            return false;
        }
    }
    return true;
}

bool WalkingQPIK_osqp::setLinearConstraintMatrix()
{
    iDynTree::Triplets constraintsTriplets;
    constraintsTriplets.addSubMatrix(0, 0, m_leftFootJacobian);
    constraintsTriplets.addSubMatrix(6, 0, m_rightFootJacobian);

    if(m_useCoMAsConstraint)
    {
        constraintsTriplets.addSubMatrix(12, 0, m_comJacobian);
        iDynTreeHelper::Triplets::pushTripletsAsSubMatrix(15, 0,
                                                          m_jointRegularizationLinearConstraintTriplets,
                                                          constraintsTriplets);
    }
    else
    {
        iDynTreeHelper::Triplets::pushTripletsAsSubMatrix(12, 0,
                                                          m_jointRegularizationLinearConstraintTriplets,
                                                          constraintsTriplets);
    }

    iDynSparseMatrix constraintsMatrix(m_numberOfConstraints, m_numberOfVariables);
    constraintsMatrix.setFromConstTriplets(constraintsTriplets);

    // convert iDynTree sparse matrix in eigen sparse matrix
    // it is required by the osqp library
    Eigen::SparseMatrix<double> constraintsMatrixEigen = iDynTree::toEigen(constraintsMatrix);

    m_constraintsMatrixEigenDense = Eigen::MatrixXd(constraintsMatrixEigen);

    if(m_optimizerSolver->isInitialized())
    {
        if(!m_optimizerSolver->updateLinearConstraintsMatrix(constraintsMatrixEigen))
        {
            yError() << "[setLinearConstraintsMatrix] Unable to update the constraints matrix.";
            return false;
        }
    }
    else
    {
        if(!m_optimizerSolver->data()->setLinearConstraintsMatrix(constraintsMatrixEigen))
        {
            yError() << "[setLinearConstraintsMatrix] Unable to set the constraints matrix.";
            return false;
        }
    }
    return true;
}

bool WalkingQPIK_osqp::setBounds()
{
    Eigen::VectorXd leftFootCorrection(6);
    leftFootCorrection.block(0,0,3,1) = m_kPosFoot * iDynTree::toEigen((m_leftFootToWorldTransform.getPosition() -
                                                                        m_desiredLeftFootToWorldTransform.getPosition()));

    iDynTree::Matrix3x3 errorLeftAttitude = iDynTreeHelper::Rotation::skewSymmetric(m_leftFootToWorldTransform.getRotation() *
                                                                                    m_desiredLeftFootToWorldTransform.getRotation().inverse());

    leftFootCorrection.block(3,0,3,1) = m_kAttFoot * (iDynTree::unskew(iDynTree::toEigen(errorLeftAttitude)));

    Eigen::VectorXd rightFootCorrection(6);
    rightFootCorrection.block(0,0,3,1) = m_kPosFoot * iDynTree::toEigen((m_rightFootToWorldTransform.getPosition() -
                                                                         m_desiredRightFootToWorldTransform.getPosition()));

    iDynTree::Matrix3x3 errorRightAttitude = iDynTreeHelper::Rotation::skewSymmetric(m_rightFootToWorldTransform.getRotation() *
                                                                                     m_desiredRightFootToWorldTransform.getRotation().inverse());

    rightFootCorrection.block(3,0,3,1) = m_kAttFoot * (iDynTree::unskew(iDynTree::toEigen(errorRightAttitude)));

    if((m_leftFootTwist(0) == m_leftFootTwist(1)) && (m_leftFootTwist(0) == 0))
    {
        m_lowerBound.block(0, 0, 6, 1) = iDynTree::toEigen(m_leftFootTwist);
        m_upperBound.block(0, 0, 6, 1) = iDynTree::toEigen(m_leftFootTwist);
    }
    else
    {
        m_lowerBound.block(0, 0, 6, 1) = iDynTree::toEigen(m_leftFootTwist) - leftFootCorrection;
        m_upperBound.block(0, 0, 6, 1) = iDynTree::toEigen(m_leftFootTwist) - leftFootCorrection;
    }

    if((m_rightFootTwist(0) == m_rightFootTwist(1)) && (m_rightFootTwist(0) == 0))
    {
        m_lowerBound.block(6, 0, 6, 1) = iDynTree::toEigen(m_rightFootTwist);
        m_upperBound.block(6, 0, 6, 1) = iDynTree::toEigen(m_rightFootTwist);
    }
    else
    {
        m_lowerBound.block(6, 0, 6, 1) = iDynTree::toEigen(m_rightFootTwist) - rightFootCorrection;
        m_upperBound.block(6, 0, 6, 1) = iDynTree::toEigen(m_rightFootTwist) - rightFootCorrection;
    }
    if(m_useCoMAsConstraint)
    {
        m_lowerBound.block(12, 0, 3, 1) = iDynTree::toEigen(m_comVelocity)
            - m_kCom * (iDynTree::toEigen(m_comPosition) -  iDynTree::toEigen(m_desiredComPosition));
        m_upperBound.block(12, 0, 3, 1) = iDynTree::toEigen(m_comVelocity)
            - m_kCom * (iDynTree::toEigen(m_comPosition) -  iDynTree::toEigen(m_desiredComPosition));
    }

    // set joint velocity bounds
    for(int i = m_numberOfTaskConstraints; i < m_numberOfConstraints; i++)
    {
        int index = i - m_numberOfTaskConstraints;

        m_lowerBound(i) = std::tanh(m_kb * (m_jointPosition(index) - m_minJointsPosition(index)))
            * (-m_maxJointsVelocity(index));

        m_upperBound(i) = std::tanh(m_ku *(m_maxJointsPosition(index) - m_jointPosition(index)))
            * (m_maxJointsVelocity(index));
    }



    if(m_optimizerSolver->isInitialized())
    {
        if(!m_optimizerSolver->updateBounds<Eigen::Dynamic>(m_lowerBound, m_upperBound))
        {
            yError() << "[setBounds] Unable to update the bounds.";
            return false;
        }
    }
    else
    {
        if(!m_optimizerSolver->data()->setLowerBound<Eigen::Dynamic>(m_lowerBound))
        {
            yError() << "[setBounds] Unable to set the first time the lower bound.";
            return false;
        }

        if(!m_optimizerSolver->data()->setUpperBound<Eigen::Dynamic>(m_upperBound))
        {
            yError() << "[setBounds] Unable to set the first time the upper bound.";
            return false;
        }
    }
    return true;
}

bool WalkingQPIK_osqp::solve()
{
    m_isSolutionEvaluated = false;

    if(!setHessianMatrix())
    {
        yError() << "[solve] Unable to set the hessian matrix.";
        return false;
    }

    if(!setGradientVector())
    {
        yError() << "[solve] Unable to set the gradient vector matrix.";
        return false;
    }

    if(!setLinearConstraintMatrix())
    {
        yError() << "[solve] Unable to set the linear constraint matrix.";
        return false;
    }

    if(!setBounds())
    {
        yError() << "[solve] Unable to set the bounds.";
        return false;
    }

    if(!m_optimizerSolver->isInitialized())
    {
        if(!m_optimizerSolver->initSolver())
        {
            yError() << "[solve] Unable to initialize the solver";
            return false;
        }
    }

    if(!m_optimizerSolver->solve())
    {
        yError() << "[solve] Unable to solve the problem.";
        return false;
    }

    // check equality constraints
    if(!isSolutionFeasible())
    {
        yError() << "[solve] The solution is not feasible.";
        return false;
    }

    m_isSolutionEvaluated = true;

    return true;
}

bool WalkingQPIK_osqp::isSolutionFeasible()
{
    double tolerance = 1;

    Eigen::VectorXd constrainedOutput = m_constraintsMatrixEigenDense
        * m_optimizerSolver->getSolution();

    if(((constrainedOutput - m_upperBound).maxCoeff() < tolerance)
       ||((constrainedOutput - m_lowerBound).maxCoeff() > -tolerance))
        return true;

    yError() << "[isSolutionFeasible] The constraints are not satisfied.";
    return false;
}

bool WalkingQPIK_osqp::getSolution(iDynTree::VectorDynSize& output)
{
    if(!m_isSolutionEvaluated)
    {
        yError() << "[getSolution] The solution is not evaluated. "
                 << "Please call 'solve()' method.";
        return false;
    }

    if(output.size() != m_actuatedDOFs)
        output.resize(m_actuatedDOFs);

    Eigen::VectorXd solutionEigen = m_optimizerSolver->getSolution();

    for(int i = 0; i < output.size(); i++)
        output(i) = solutionEigen(i + 6);

    return true;
}

bool WalkingQPIK_osqp::getLeftFootError(iDynTree::VectorDynSize& output)
{
    if(!m_isSolutionEvaluated)
    {
        yError() << "[getLeftFootError] The solution is not evaluated. "
                 << "Please call 'solve()' method.";
        return false;
    }

    iDynTree::toEigen(output) = m_lowerBound.block(0, 0, 6, 1) - iDynTree::toEigen(m_leftFootJacobian) *  m_optimizerSolver->getSolution();
    return true;
}

bool WalkingQPIK_osqp::getRightFootError(iDynTree::VectorDynSize& output)
{
    if(!m_isSolutionEvaluated)
    {
        yError() << "[getRightFootError] The solution is not evaluated. "
                 << "Please call 'solve()' method.";
        return false;
    }

    iDynTree::toEigen(output) = m_lowerBound.block(6, 0, 6, 1) - iDynTree::toEigen(m_rightFootJacobian) *  m_optimizerSolver->getSolution();
    return true;
}

const Eigen::MatrixXd& WalkingQPIK_osqp::getHessianMatrix() const
{
    return m_hessianEigenDense;
}

const Eigen::MatrixXd& WalkingQPIK_osqp::getConstraintMatrix() const
{
    return m_constraintsMatrixEigenDense;
}

const Eigen::VectorXd& WalkingQPIK_osqp::getUpperBound() const
{
    return m_upperBound;
}

const Eigen::VectorXd& WalkingQPIK_osqp::getLowerBound() const
{
    return m_lowerBound;
}

const Eigen::VectorXd& WalkingQPIK_osqp::getGradient() const
{
    return m_gradient;
}
