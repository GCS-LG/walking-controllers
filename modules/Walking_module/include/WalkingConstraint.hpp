/**
 * @file WalkingConstraint.hpp
 * @authors Giulio Romualdi <giulio.romualdi@iit.it>
 * @copyright 2018 iCub Facility - Istituto Italiano di Tecnologia
 *            Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 * @date 2018
 */

#ifndef WALKING_CONSTRAINT_HPP
#define WALKING_CONSTRAINT_HPP

// std
#include <memory>
#include <unordered_map>

// iDynTree
#include <iDynTree/Core/VectorDynSize.h>
#include <iDynTree/Core/MatrixDynSize.h>
#include <iDynTree/Core/Transform.h>

#include <Utils.hpp>
#include <CartesianPID.hpp>

#include <TimeProfiler.hpp>

enum class CartesianElementType {POSE, POSITION, ORIENTATION, ONE_DIMENSION};


/**
 * GenericCartesianElement
 */
class CartesianElement
{

protected:
    /**
     * Evaluate the desired acceleration. It depends on the type of constraint (Positional,
     * Rotational)
     */
    void evaluateDesiredAcceleration();


    bool m_isActive{true}; /**< True if the element is active */

    iDynTree::VectorDynSize const * m_biasAcceleration; /**< Bias acceleration J \nu. */
    iDynTree::MatrixDynSize const * m_roboticJacobian; /**< Robotic Jacobian in mixed representation. */
    iDynTree::VectorDynSize m_desiredAcceleration; /**< Desired acceleration evaluated by the
                                                      controller. */
    std::unordered_map<std::string, std::shared_ptr<CartesianPID>> m_controllers; /**< Set of
                                                                                     controllers. */
    CartesianElementType m_elementType;

public:

    CartesianElement(const CartesianElementType& elementType);

    /**
     * Set bias acceleration
     * @param biasAcceleration bias acceleration \f$ \dot{J} \nu $\f
     */
    void setBiasAcceleration(const iDynTree::VectorDynSize& biasAcceleration){m_biasAcceleration = &biasAcceleration;};

    /**
     * Set the jacobian (robot)
     * @param roboticJacobian standard jacobian used to map the end-effector velocity to the robot velocity
     * (MIXED representation)
     */
    void setRoboticJacobian(const iDynTree::MatrixDynSize& roboticJacobian){m_roboticJacobian = &roboticJacobian;};

    /**
     * Get the position controller associated to the constraint.
     * @return pointer to the controller.
     */
    std::shared_ptr<LinearPID> positionController();

    /**
     * Get the orientation controller associated to the constraint.
     * @return pointer to the controller.
     */
    std::shared_ptr<RotationalPID> orientationController();

    void setState(bool state){m_isActive = state;};
};


class OptimizationElement
{
protected:

    bool m_firstTime{true};

    int m_jacobianStartingRow; /**< Staring row of the jacobian sub-matrix.*/
    int m_jacobianStartingColumn; /**< Staring column of the jacobian sub-matrix.*/

    int m_hessianStartingRow; /**< Staring row of the hessian sub-matrix.*/
    int m_hessianStartingColumn; /**< Staring column of the hessian submatrix.*/

    int m_sizeOfElement;
public:

    /**
     * Evaluate Hessian.
     */
    virtual void evaluateHessian(Eigen::SparseMatrix<double>& hessian){;};

    /**
     * Evaluate Jacobian.
     */
    virtual void evaluateGradient(Eigen::VectorXd& gradient){;};

    /**
     * Evaluate Jacobian.
     */
    virtual void evaluateJacobian(Eigen::SparseMatrix<double>& jacobian){;};

    /**
     * Evaluate lower and upper bounds.
     */
    virtual void evaluateBounds(Eigen::VectorXd &upperBounds, Eigen::VectorXd &lowerBounds){;};

    /**
     * Set the jacobian and hessian starting row and column.
     * @param staringRow staring row of the jacobian sub-block;
     * @param staringColumn staring row of the jacobian sub-block.
     */
    void setSubMatricesStartingPosition(const int& startingRow, const int& startingColumn);

    int getJacobianStartingRow() {return m_jacobianStartingRow;};

    int getJacobianStartingColumn() {return m_jacobianStartingColumn;};
};

class Constraint : public OptimizationElement
{

public:
    /**
     * Get the number of constraint
     */
    int getNumberOfConstraints() {return m_sizeOfElement;};
};

/**
 * Linear constraint class. It handles the linear constraints.
 */
class LinearConstraint : public Constraint
{
};

/**
 * CartesianConstraint is an abstract class useful to manage a generic Cartesian constraint
 * i.e. foot position and orientation, CoM position.
 */
class CartesianConstraint : public LinearConstraint, public CartesianElement
{

public:

    CartesianConstraint(const CartesianElementType& elementType);

    /**
     * Evaluate the constraint jacobian
     */
    void evaluateJacobian(Eigen::SparseMatrix<double>& jacobian) override;

    /**
     * Evaluate lower and upper bounds.
     */
    void evaluateBounds(Eigen::VectorXd &upperBounds, Eigen::VectorXd &lowerBounds) override;
};

/**
 * ForceConstraint class allows to obtain a contact force that satisfies the unilateral constraint,
 * the friction cone and the COP position.
 */
class ForceConstraint : public LinearConstraint
{
    bool m_isActive;

    Eigen::MatrixXd m_transform;

    double m_staticFrictionCoefficient; /**< Static linear coefficient of friction */
    double m_numberOfPoints; /**< Number of points in each quadrants for linearizing friction cone */
    double m_torsionalFrictionCoefficient; /**< Torsional coefficient of friction */
    double m_minimalNormalForce; /**< Minimal positive vertical force at contact */

    iDynTree::Vector2 m_footLimitX; /**< Physical size of the foot (x axis) */
    iDynTree::Vector2 m_footLimitY; /**< Physical size of the foot (y axis) */

    bool m_isJacobianEvaluated; /**< True if the Jacobian is evaluated. */
    bool m_areBoundsEvaluated; /**< True if the bounds are evaluated. */

    //todo
    iDynSparseMatrix m_jacobianLeftTrivialized;

    iDynTree::Transform const * m_footToWorldTransform;

public:

    /**
     * Constructor
     * @param numberOfPoints number of points used to approximated the friction cone
     */
    ForceConstraint(const int& numberOfPoints);

    /**
     * Set the static friction cone coefficient
     * @param staticFrictionCoefficient static friction coefficient.
     */
    void setStaticFrictionCoefficient(const double& staticFrictionCoefficient){m_staticFrictionCoefficient = staticFrictionCoefficient;};

    /**
     * Set the torsional friction coefficient
     * @param torsionalFrictionCoefficient torsional friction coefficient.
     */
    void setTorsionalFrictionCoefficient(const double& torsionalFrictionCoefficient){m_torsionalFrictionCoefficient = torsionalFrictionCoefficient;};

    /**
     * Set minimal normal force
     * @param minimalNormalForce minimal normal force. It has to be a positive number
     */
    void setMinimalNormalForce(const double& minimalNormalForce){m_minimalNormalForce = minimalNormalForce;};

    /**
     * Set the size of the foot
     * @param footLimitX vector containing the max and the min X coordinates
     * @param footLimitY vector containing the max and the min y coordinates
     */
    void setFootSize(const iDynTree::Vector2& footLimitX, const iDynTree::Vector2& footLimitY);

    // todo
    void setFootToWorldTransform(const iDynTree::Transform& footToWorldTransform){m_footToWorldTransform = &footToWorldTransform;};

    void setFootState(bool footState){m_isActive = footState;};
    /**
     * Evaluate the jacobian
     */
    void evaluateJacobian(Eigen::SparseMatrix<double>& jacobian) override;

    /**
     * Evaluate the lower and upper bounds
     */
    void evaluateBounds(Eigen::VectorXd &upperBounds, Eigen::VectorXd &lowerBounds) override;
};

/**
 * ZMP class allows to obtain a contact force that satisfies the desired ZMP position
 */
class ZMPConstraint : public LinearConstraint
{
    iDynTree::Transform const * m_leftFootToWorldTransform;
    iDynTree::Transform const * m_rightFootToWorldTransform;

    iDynTree::Vector2 m_desiredZMP;
    bool m_areBoundsEvaluated = false;

    bool m_isRightFootOnGround{true};
    bool m_isLeftFootOnGround{true};


public:

    ZMPConstraint();

    void setRightFootState(bool isRightFootOnGround){m_isRightFootOnGround = isRightFootOnGround;};
    void setLeftFootState(bool isLeftFootOnGround){m_isLeftFootOnGround = isLeftFootOnGround;};

    /**
     * Set the desired ZMP
     * @param zmp desired ZMP
     */
    void setDesiredZMP(const iDynTree::Vector2& zmp){m_desiredZMP = zmp;};

    /**
     * Set the left foot to world transformation
     * @param leftFootToWorldTransform tranformation between the left foot and the world frame world_H_leftFoot
     */
    void setLeftFootToWorldTransform(const iDynTree::Transform& leftFootToWorldTransform){m_leftFootToWorldTransform = &leftFootToWorldTransform;};

    /**
     * Set the right foot to world transformation
     * @param rightFootToWorldTransform tranformation between the right foot and the world frame world_H_rightFoot
     */
    void setRightFootToWorldTransform(const iDynTree::Transform& rightFootToWorldTransform){m_rightFootToWorldTransform = &rightFootToWorldTransform;};

    /**
     * Evaluate the jacobian
     */
    void evaluateJacobian(Eigen::SparseMatrix<double>& jacobian) override;

    /**
     * Evaluate the lower and upper bounds
     */
    void evaluateBounds(Eigen::VectorXd &upperBounds, Eigen::VectorXd &lowerBounds) override;
};

/**
 * Please do not use me! I am not implemented yet!
 */
class LinearMomentumConstraint : public LinearConstraint
{
    double m_robotMass;

    std::shared_ptr<LinearPID> m_controller;

public:

    LinearMomentumConstraint();

    void setRobotMass(const double& robotMass){m_robotMass = robotMass;};

    /**
     * Evaluate the jacobian
     */
    void evaluateJacobian(Eigen::SparseMatrix<double>& jacobian) override;

    /**
     * Evaluate the lower and upper bounds
     */
    void evaluateBounds(Eigen::VectorXd &upperBounds, Eigen::VectorXd &lowerBounds) override;

    std::shared_ptr<LinearPID> controller() {return m_controller;};
};

class AngularMomentumConstraint : public LinearConstraint
{
    std::shared_ptr<LinearPID> m_controller;

    iDynTree::Position const * m_comPosition; /**< . */
    iDynTree::Transform const * m_leftFootToWorldTransform; /**< Left foot to world transformation*/
    iDynTree::Transform const * m_rightFootToWorldTransform; /**< Right foot to world transformation. */

public:

    AngularMomentumConstraint();

    void setCoMPosition(const iDynTree::Position& comPosition){m_comPosition = &comPosition;};

    void setLeftFootToWorldTransform(const iDynTree::Transform& leftFootToWorldTransform){m_leftFootToWorldTransform = &leftFootToWorldTransform;};

    void setRightFootToWorldTransform(const iDynTree::Transform& rightFootToWorldTransform){m_rightFootToWorldTransform = &rightFootToWorldTransform;};

    std::shared_ptr<LinearPID> controller() {return m_controller;};

    /**
     * Evaluate the jacobian
     */
    void evaluateJacobian(Eigen::SparseMatrix<double>& jacobian) override;

    /**
     * Evaluate the lower and upper bounds
     */
    void evaluateBounds(Eigen::VectorXd &upperBounds, Eigen::VectorXd &lowerBounds) override;
};


/**
 *
 */
class SystemDynamicConstraint : public LinearConstraint
{
    iDynTree::MatrixDynSize const * m_massMatrix;
    iDynTree::MatrixDynSize const * m_leftFootJacobian;
    iDynTree::MatrixDynSize const * m_rightFootJacobian;
    iDynTree::VectorDynSize const * m_generalizedBiasForces;

    int m_systemSize;
    iDynSparseMatrix m_selectionMatrix;

    bool m_isRightFootOnGround{true};
    bool m_isLeftFootOnGround{true};

public:

    SystemDynamicConstraint(const int& systemSize);

    void setRightFootState(bool isRightFootOnGround){m_isRightFootOnGround = isRightFootOnGround;};
    void setLeftFootState(bool isLeftFootOnGround){m_isLeftFootOnGround = isLeftFootOnGround;};

    void setLeftFootJacobian(const iDynTree::MatrixDynSize& leftFootJacobian){m_leftFootJacobian = &leftFootJacobian;};

    void setRightFootJacobian(const iDynTree::MatrixDynSize& rightFootJacobian){m_rightFootJacobian = &rightFootJacobian;};

    void setMassMatrix(const iDynTree::MatrixDynSize& massMatrix){m_massMatrix = &massMatrix;};

    void setGeneralizedBiasForces(const iDynTree::VectorDynSize& generalizedBiasForces){m_generalizedBiasForces = &generalizedBiasForces;};

    /**
     * Evaluate the constraint jacobian
     */
    void evaluateJacobian(Eigen::SparseMatrix<double>& jacobian) override;

    /**
     * Evaluate lower and upper bounds.
     */
    void evaluateBounds(Eigen::VectorXd &upperBounds, Eigen::VectorXd &lowerBounds) override;
};

class RateOfChangeConstraint : public LinearConstraint
{
    iDynTree::VectorDynSize m_maximumRateOfChange;
    iDynTree::VectorDynSize const * m_previousValues;

public:

    RateOfChangeConstraint(const int& sizeOfTheCOnstraintVector);

    void setMaximumRateOfChange(const iDynTree::VectorDynSize& maximumRateOfChange){m_maximumRateOfChange = maximumRateOfChange;};

    void setPreviousValues(const iDynTree::VectorDynSize& previousValues){m_previousValues = &previousValues;};

    /**
     * Evaluate the constraint jacobian
     */
    void evaluateJacobian(Eigen::SparseMatrix<double>& jacobian) override;

    /**
     * Evaluate lower and upper bounds.
     */
    void evaluateBounds(Eigen::VectorXd &upperBounds, Eigen::VectorXd &lowerBounds) override;
};

class CostFunctionElement : public OptimizationElement
{
protected:
    iDynTree::VectorDynSize m_weight;

public:
    void setWeight(const iDynTree::VectorDynSize& weight) {m_weight = weight;};
};

class QuadraticCostFunction : public CostFunctionElement
{
};

class CartesianCostFunction : public QuadraticCostFunction,
                              public CartesianElement
{

    Eigen::MatrixXd m_hessianSubMatrix;
    Eigen::MatrixXd m_gradientSubMatrix;

public:
    CartesianCostFunction(const CartesianElementType& elementType);

    /**
     * Evaluate the gradient vector
     */
    void evaluateGradient(Eigen::VectorXd& gradient);

    /**
     * Evaluate the Hessian matrix
     */
    void evaluateHessian(Eigen::SparseMatrix<double>& hessian) override;
};

class JointRegularizationTerm : public QuadraticCostFunction
{

    iDynTree::VectorDynSize m_derivativeGains;
    iDynTree::VectorDynSize m_proportionalGains;

    iDynTree::VectorDynSize const * m_desiredJointPosition;
    iDynTree::VectorDynSize const * m_desiredJointVelocity;
    iDynTree::VectorDynSize const * m_desiredJointAcceleration;
    iDynTree::VectorDynSize const * m_jointPosition;
    iDynTree::VectorDynSize const * m_jointVelocity;

public:
    JointRegularizationTerm(const int &systemSize){m_sizeOfElement = systemSize;};

    void setDerivativeGains(const iDynTree::VectorDynSize &derivativeGains){m_derivativeGains = derivativeGains;};

    void setProportionalGains(const iDynTree::VectorDynSize &proportionalGains){m_proportionalGains = proportionalGains;};

    void setDesiredJointPosition(const iDynTree::VectorDynSize &desiredJointPosition){m_desiredJointPosition = &desiredJointPosition;};

    void setDesiredJointVelocity(const iDynTree::VectorDynSize &desiredJointVelocity){m_desiredJointVelocity = &desiredJointVelocity;};

    void setDesiredJointAcceleration(const iDynTree::VectorDynSize &desiredJointAcceleration){m_desiredJointAcceleration = &desiredJointAcceleration;};

    void setJointPosition(const iDynTree::VectorDynSize &jointPosition){m_jointPosition = &jointPosition;};

    void setJointVelocity(const iDynTree::VectorDynSize &jointVelocity){m_jointVelocity = &jointVelocity;};

    /**
     * Evaluate the Hessian matrix
     */
    void evaluateHessian(Eigen::SparseMatrix<double>& hessian) override;

    /**
     * Evaluate the Hessian matrix
     */
    void evaluateGradient(Eigen::VectorXd& gradient);
};

class InputRegularizationTerm : public QuadraticCostFunction
{

public:
    InputRegularizationTerm(const int &systemSize){m_sizeOfElement = systemSize;};

    /**
     * Evaluate the Hessian matrix
     */
    void evaluateHessian(Eigen::SparseMatrix<double>& hessian) override;
};


#endif
