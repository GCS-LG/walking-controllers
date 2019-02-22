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


/**
 * Generic Cartesian Element
 */
class CartesianElement
{
public:
    /** Cartesian Element Type */
    enum class Type {POSE, POSITION, ORIENTATION, ONE_DIMENSION, CONTACT};
protected:
    iDynTree::VectorDynSize const * m_biasAcceleration; /**< Bias acceleration J \nu. */
    iDynTree::MatrixDynSize const * m_roboticJacobian; /**< Robotic Jacobian in mixed representation. */
    iDynTree::VectorDynSize m_desiredAcceleration; /**< Desired acceleration evaluated by the
                                                      controller. */
    std::unordered_map<std::string, std::shared_ptr<CartesianPID>> m_controllers; /**< Set of
                                                                                     controllers. */

    Type m_elementType; /**< Type of the Cartesian element */

    /**
     * Evaluate the desired acceleration. It depends on the type of constraint (Positional,
     * Rotational)
     */
    void evaluateDesiredAcceleration();
public:

    /**
     * Constructor of the CartesianElement class
     * @param elementType type of Cartesian element it can be POSE, POSITION, ORIENTATION, ONE_DIMENSION, CONTACT
     */
    CartesianElement(const Type& elementType);

    /**
     * Set bias acceleration
     * @param biasAcceleration bias acceleration \f$ \dot{J} \nu $\f
     */
    inline void setBiasAcceleration(const iDynTree::VectorDynSize& biasAcceleration){m_biasAcceleration = &biasAcceleration;};

    /**
     * Set the jacobian (robot)
     * @param roboticJacobian standard jacobian used to map the end-effector velocity to the robot velocity
     * (MIXED representation)
     */
    inline void setRoboticJacobian(const iDynTree::MatrixDynSize& roboticJacobian){m_roboticJacobian = &roboticJacobian;};

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
};


/**
 * Optimization element is a virtual class that implements a simple optimization element.
 */
class OptimizationElement
{
protected:

    int m_jacobianStartingRow; /**< Staring row of the jacobian sub-matrix.*/
    int m_jacobianStartingColumn; /**< Staring column of the jacobian sub-matrix.*/

    int m_hessianStartingRow; /**< Staring row of the hessian sub-matrix.*/
    int m_hessianStartingColumn; /**< Staring column of the hessian sub-matrix.*/

    int m_sizeOfElement; /**< Size of the optimization element.*/
public:

    /**
     * Evaluate Hessian.
     * @param hessian hessian matrix
     */
    virtual void evaluateHessian(Eigen::SparseMatrix<double>& hessian){;};

    /**
     * Evaluate the gradient.
     * @param gradient gradient vector
     */
    virtual void evaluateGradient(Eigen::VectorXd& gradient){;};

    /**
     * Evaluate Jacobian.
     * @param jacobian jacobian matrix
     */
    virtual void evaluateJacobian(Eigen::SparseMatrix<double>& jacobian){;};

    /**
     * Evaluate lower and upper bounds.
     * @param upperBounds vector containing the constraint upper bounds
     * @param lowerBounds vector containing the constraint lower bounds
     */
    virtual void evaluateBounds(Eigen::VectorXd &upperBounds, Eigen::VectorXd &lowerBounds){;};

    /**
     * Set the constant elements of the Hessian matrix.
     * @param hessian hessian matrix
     */
    virtual void setHessianConstantElements(Eigen::SparseMatrix<double>& hessian){;};

    /**
     * Set the constant elements of the Jacobian matrix.
     * @param jacobian jacobian matrix
     */
    virtual void setJacobianConstantElements(Eigen::SparseMatrix<double>& jacobian){;};

    /**
     * Set the constant elements of the Gradient vector.
     * @param gradient gradient vector
     */
    virtual void setGradientConstantElemenets(Eigen::VectorXd& gradient){;};

    /**
     * Set the constant elements of the upper and lower bounds
     * @param upperBounds vector containing the constraint upper bounds
     * @param lowerBounds vector containing the constraint lower bounds
     */
    virtual void setBoundsConstantElements(Eigen::VectorXd &upperBounds, Eigen::VectorXd &lowerBounds){;};

    /**
     * Set the jacobian and hessian starting row and column.
     * @param staringRow staring row of the jacobian sub-block;
     * @param staringColumn staring row of the jacobian sub-block.
     */
    void setSubMatricesStartingPosition(const int& startingRow, const int& startingColumn);

    /**
     * Get the jacobian starting row
     * @return return the index of the staring row of the jacobian
     */
    inline int getJacobianStartingRow() {return m_jacobianStartingRow;};

    /**
     * Get the jacobian starting column
     * @return return the index of the staring columns of the jacobian
     */
    inline int getJacobianStartingColumn() {return m_jacobianStartingColumn;};
};

/**
 * Constraint class
 */
class Constraint : public OptimizationElement
{

public:
    /**
     * Get the number of constraint
     * @return the number of constraint
     */
    inline int getNumberOfConstraints() {return m_sizeOfElement;};
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

    CartesianConstraint(const Type& elementType);

    /**
     * Evaluate the constraint jacobian
     */
    void evaluateJacobian(Eigen::SparseMatrix<double>& jacobian) override;

    /**
     * Set lower and upper bounds constant elements.
     */
    void evaluateBounds(Eigen::VectorXd &upperBounds, Eigen::VectorXd &lowerBounds) override;
};

/**
 * ForceConstraint class allows to obtain a contact force that satisfies the unilateral constraint,
 * the friction cone and the COP position.
 */
class ForceConstraint : public LinearConstraint
{
    bool m_areBoundsEvaluated; /**< True if the bounds are evaluated. */

    //todo
    iDynSparseMatrix m_jacobianLeftTrivialized;

    iDynTree::Transform const * m_footToWorldTransform;

    iDynTree::VectorDynSize m_upperBound;
    iDynTree::VectorDynSize m_lowerBound;

public:

    /**
     * Constructor
     * @param numberOfPoints number of points used to approximated the friction cone
     * @param staticFrictionCoefficient static friction coefficient.
     * @param torsionalFrictionCoefficient torsional friction coefficient.
     * @param minimalNormalForce minimal normal force. It has to be a positive number
     * @param footLimitX vector containing the max and the min X coordinates
     * @param footLimitY vector containing the max and the min y coordinates
     */
    ForceConstraint(const int& numberOfPoints, const double& staticFrictionCoefficient,
                    const double& torsionalFrictionCoefficient, const double& minimalNormalForce,
                    const iDynTree::Vector2& footLimitX, const iDynTree::Vector2& footLimitY);


    // todo
    void setFootToWorldTransform(const iDynTree::Transform& footToWorldTransform){m_footToWorldTransform = &footToWorldTransform;};

    /**
     * Evaluate the jacobian
     */
    void evaluateJacobian(Eigen::SparseMatrix<double>& jacobian) override;

    /**
     * Set constant elemenets of the lower and upper boulds
     */
    void setBoundsConstantElements(Eigen::VectorXd &upperBounds, Eigen::VectorXd &lowerBounds) override;
};

/**
 * ZMP class allows to obtain a contact force that satisfies the desired ZMP position
 */
class ZMPConstraint : public LinearConstraint
{
protected:

    iDynTree::Vector2 m_desiredZMP;
    iDynTree::Vector2 m_measuredZMP;
    iDynTree::Vector2 m_kp;

public:
    ZMPConstraint();

    /**
     * Set the desired ZMP
     * @param zmp desired ZMP
     */
    void setDesiredZMP(const iDynTree::Vector2& zmp){m_desiredZMP = zmp;};

    /**
     * Set the measured ZMP
     * @param zmp measured ZMP
     */
    void setMeasuredZMP(const iDynTree::Vector2& zmp){m_measuredZMP = zmp;};


    void setKp(const iDynTree::Vector2& kp){m_kp = kp;};

    /**
     * Evaluate the lower and upper bounds
     */
    void setBoundsConstantElements(Eigen::VectorXd &upperBounds, Eigen::VectorXd &lowerBounds) override;
};

class ZMPConstraintDoubleSupport : public ZMPConstraint
{
    iDynTree::Transform const * m_leftFootToWorldTransform;
    iDynTree::Transform const * m_rightFootToWorldTransform;

public:

    ZMPConstraintDoubleSupport() : ZMPConstraint() {};

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
};

class ZMPConstraintSingleSupport : public ZMPConstraint
{
    iDynTree::Transform const * m_stanceFootToWorldTransform;

public:

    ZMPConstraintSingleSupport() : ZMPConstraint() {};

    /**
     * Set the left foot to world transformation
     * @param leftFootToWorldTransform tranformation between the left foot and the world frame world_H_leftFoot
     */
    void setStanceFootToWorldTransform(const iDynTree::Transform& stanceFootToWorldTransform){m_stanceFootToWorldTransform = &stanceFootToWorldTransform;};

    /**
     * Evaluate the jacobian
     */
    void evaluateJacobian(Eigen::SparseMatrix<double>& jacobian) override;
};

class LinearMomentumElement
{
public:
    enum class Type {SINGLE_SUPPORT, DOUBLE_SUPPORT};

protected:
    Type m_elementType;
    double m_robotMass;
    iDynTree::Position m_comPosition;
    iDynTree::Vector3 m_desiredVRPPosition;

public:
    LinearMomentumElement(const Type& elementType) : m_elementType(elementType){};

    void setRobotMass(const double& robotMass){m_robotMass = robotMass;};

    void setCoMPosition(const iDynTree::Position& comPosition){m_comPosition = comPosition;};

    void setDesiredVRP(const iDynTree::Vector3& desiredVRPPosition){m_desiredVRPPosition = desiredVRPPosition;};
};

class LinearMomentumConstraint : public LinearConstraint, public LinearMomentumElement
{
public:

    LinearMomentumConstraint(const Type& elementType);

    /**
     * Evaluate the jacobian
     */
    void setJacobianConstantElements(Eigen::SparseMatrix<double>& jacobian) override;

    /**
     * Evaluate the lower and upper bounds
     */
    void evaluateBounds(Eigen::VectorXd &upperBounds, Eigen::VectorXd &lowerBounds) override;
};

class AngularMomentumElement
{
protected:
    iDynTree::Position m_comPosition;
    double m_kp;
    iDynTree::Vector3 m_angularMomentum;

    iDynTree::Vector3 desiredAngularMomentumRateOfChange();

public:
    void setKp(const double& kp){m_kp = kp;};

    void setCoMPosition(const iDynTree::Position& comPosition){m_comPosition = comPosition;};

    void setAngularMomentum(const iDynTree::Vector3& angularMomentum){m_angularMomentum = angularMomentum;};

};

class AngularMomentumElementSingleSupport : public AngularMomentumElement
{
protected:
    iDynTree::Transform const * m_stanceFootToWorldTransform; /**< Left foot to world transformation*/

public:

    void setStanceFootToWorldTransform(const iDynTree::Transform& stanceFootToWorldTransform){m_stanceFootToWorldTransform = &stanceFootToWorldTransform;};
};

class AngularMomentumElementDoubleSupport : public AngularMomentumElement
{
protected:
    iDynTree::Transform const * m_leftFootToWorldTransform; /**< Left foot to world transformation*/
    iDynTree::Transform const * m_rightFootToWorldTransform; /**< Right foot to world transformation */
public:

       void setLeftFootToWorldTransform(const iDynTree::Transform& leftFootToWorldTransform){m_leftFootToWorldTransform = &leftFootToWorldTransform;};

    void setRightFootToWorldTransform(const iDynTree::Transform& rightFootToWorldTransform){m_rightFootToWorldTransform = &rightFootToWorldTransform;};

};

class AngularMomentumConstraintSingleSupport : public LinearConstraint, public AngularMomentumElementSingleSupport
{
public:
    AngularMomentumConstraintSingleSupport();
    /**
     * Evaluate the jacobian
     */
    void setJacobianConstantElements(Eigen::SparseMatrix<double>& jacobian) override;

    /**
     * Evaluate the jacobian
     */
    void evaluateJacobian(Eigen::SparseMatrix<double>& jacobian) override;

    /**
     * Evaluate the lower and upper bounds
     */
    void evaluateBounds(Eigen::VectorXd &upperBounds, Eigen::VectorXd &lowerBounds) override;
};

class AngularMomentumConstraintDoubleSupport : public LinearConstraint, public AngularMomentumElementDoubleSupport
{

public:
    AngularMomentumConstraintDoubleSupport();


    /**
     * Evaluate the jacobian
     */
    void setJacobianConstantElements(Eigen::SparseMatrix<double>& jacobian) override;

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
    iDynTree::VectorDynSize const * m_generalizedBiasForces;

protected:
    iDynTree::MatrixDynSize const * m_massMatrix;
    int m_systemSize;

public:

    SystemDynamicConstraint(const int& systemSize);

    void setMassMatrix(const iDynTree::MatrixDynSize& massMatrix){m_massMatrix = &massMatrix;};

    void setGeneralizedBiasForces(const iDynTree::VectorDynSize& generalizedBiasForces){m_generalizedBiasForces = &generalizedBiasForces;};

    /**
     * Evaluate lower and upper bounds.
     */
    void evaluateBounds(Eigen::VectorXd &upperBounds, Eigen::VectorXd &lowerBounds) override;

    void setJacobianConstantElements(Eigen::SparseMatrix<double>& jacobian) override;
};

class SystemDynamicConstraintDoubleSupport : public SystemDynamicConstraint
{
    iDynTree::MatrixDynSize const * m_leftFootJacobian;
    iDynTree::MatrixDynSize const * m_rightFootJacobian;

public:

    SystemDynamicConstraintDoubleSupport(const int& systemSize) : SystemDynamicConstraint(systemSize){};

    void setLeftFootJacobian(const iDynTree::MatrixDynSize& leftFootJacobian){m_leftFootJacobian = &leftFootJacobian;};

    void setRightFootJacobian(const iDynTree::MatrixDynSize& rightFootJacobian){m_rightFootJacobian = &rightFootJacobian;};

    /**
     * Evaluate the constraint jacobian
     */
    void evaluateJacobian(Eigen::SparseMatrix<double>& jacobian) override;

};

class SystemDynamicConstraintSingleSupport : public SystemDynamicConstraint
{
    iDynTree::MatrixDynSize const * m_stanceFootJacobian;

public:

    SystemDynamicConstraintSingleSupport(const int& systemSize) : SystemDynamicConstraint(systemSize){};

    void setStanceFootJacobian(const iDynTree::MatrixDynSize& stanceFootJacobian){m_stanceFootJacobian = &stanceFootJacobian;};

    /**
     * Evaluate the constraint jacobian
     */
    void evaluateJacobian(Eigen::SparseMatrix<double>& jacobian) override;

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
    void setJacobianConstantElements(Eigen::SparseMatrix<double>& jacobian) override;

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
    CartesianCostFunction(const Type& elementType);

    /**
     * Evaluate the gradient vector
     */
    void evaluateGradient(Eigen::VectorXd& gradient) override;

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
     * Evaluate the Gradient vector
     */
    void evaluateGradient(Eigen::VectorXd& gradient) override;
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

class LinearMomentumCostFunction : public QuadraticCostFunction,
                                   public LinearMomentumElement
{
public:

    LinearMomentumCostFunction(const Type &elemetType);

    void setHessianConstantElements(Eigen::SparseMatrix<double>& hessian) override;

    /**
     * Evaluate the Gradient vector
     */
    void evaluateGradient(Eigen::VectorXd& gradient) override;
};

class AngularMomentumCostFunctionDoubleSupport : public QuadraticCostFunction,
                                                 public AngularMomentumElementDoubleSupport
{
public:

    AngularMomentumCostFunctionDoubleSupport();


    void evaluateHessian(Eigen::SparseMatrix<double>& hessian) override;

    /**
     * Evaluate the Gradient vector
     */
    void evaluateGradient(Eigen::VectorXd& gradient) override;
};


class AngularMomentumCostFunctionSingleSupport : public QuadraticCostFunction,
                                                 public AngularMomentumElementSingleSupport
{
public:

    AngularMomentumCostFunctionSingleSupport();

    void evaluateHessian(Eigen::SparseMatrix<double>& hessian) override;

    /**
     * Evaluate the Gradient vector
     */
    void evaluateGradient(Eigen::VectorXd& gradient) override;
};

#endif
