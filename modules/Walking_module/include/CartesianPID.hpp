/**
 * @file CartesianPID.hpp
 * @authors Giulio Romualdi <giulio.romualdi@iit.it>
 * @copyright 2018 iCub Facility - Istituto Italiano di Tecnologia
 *            Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 * @date 2018
 */

#ifndef CARTESIAN_PID_HPP
#define CARTESIAN_PID_HPP

// iDynTree
#include <iDynTree/Core/VectorFixSize.h>
#include <iDynTree/Core/Rotation.h>

/**
 * CartesianPID class is a virtual class that represents a generic Cartesian PID controller.
 */
class CartesianPID
{
public:
    iDynTree::Vector3 m_desiredAcceleration; /**< Desired acceleration (feedforward). */
    iDynTree::Vector3 m_desiredVelocity;  /**< Desired velocity. */

    iDynTree::Vector3 m_velocity;  /**< Actual velocity. */

    iDynTree::Vector3 m_controllerOutput; /**< Controller output. */

public:
    /**
     * Evaluate the control output.
     */
    virtual void evaluateControl() = 0;

    /**
     * Get the controller output.
     * @return controller output.
     */
    const iDynTree::Vector3& getControl() const {return m_controllerOutput;};
};

/**
 * RotationalPID implements the Rotational PID. For further information please refers to
 * http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.62.8655&rep=rep1&type=pdf,
 * section 5.11.6, p.173
 */
class RotationalPID : public CartesianPID
{
public:
    double m_c0; /**< Rotational PID Gain. */
    double m_c1; /**< Rotational PID Gain. */
    double m_c2; /**< Rotational PID Gain. */

    iDynTree::Rotation m_desiredOrientation; /**< Desired orientation. */
    iDynTree::Rotation m_orientation; /**< Actual orientation. */

public:

    /**
     * Set rotational PID Gains
     * @param c0 pid gain;
     * @param c1 pid gain;
     * @param c2 pid gain.
     */
    void setGains(const double& c0, const double& c1, const double& c2);

    /**
     * Set the desired trajectory.
     * @param desiredAcceleration desired acceleration (rad/s^2);
     * @param desiredVelocity desired velocity (rad/s);
     * @param desiredOrientation rotation matrix
     */
    void setDesiredTrajectory(const iDynTree::Vector3 &desiredAcceleration,
                              const iDynTree::Vector3 &desiredVelocity,
                              const iDynTree::Rotation &desiredOrientation);

    /**
     * Set feedback
     * @param velocity angular velocity:
     * @param orientation rotation matrix.
     */
    void setFeedback(const iDynTree::Vector3 &velocity,
                     const iDynTree::Rotation &orientation);

    /**
     * Evaluate the control law.
     */
    void evaluateControl() override;
};

/**
 * Standard Liner position PID
 */
class LinearPID : public CartesianPID
{
    iDynTree::Vector3 m_kp; /**< Proportional gain */
    iDynTree::Vector3 m_kd; /**< Derivative gain */

    iDynTree::Vector3 m_desiredPosition; /**< Desired position. */

    iDynTree::Vector3 m_position; /**< Actual position. */

    Eigen::Vector3d m_error;
    Eigen::Vector3d m_dotError;
public:

    /**
     * Set PID Gains
     * @param kp proportional gain (scalar);
     * @param kd derivative gain (scalar).
     */
    void setGains(const double& kp, const double& kd);

    /**
     * Set PID Gains
     * @param kp proportional gain (vector);
     * @param kd derivative gain (vector).
     */
    void setGains(const iDynTree::Vector3& kp, const iDynTree::Vector3& kd);


    /**
     * Set the desired trajectory.
     * @param desiredAcceleration desired acceleration (m/s^2);
     * @param desiredVelocity desired velocity (m/s);
     * @param desiredPosition desired position (m).
     */
    void setDesiredTrajectory(const iDynTree::Vector3 &desiredAcceleration,
                              const iDynTree::Vector3 &desiredVelocity,
                              const iDynTree::Vector3 &desiredPosition);

    /**
     * Set feedback
     * @param velocity linear velocity:
     * @param position actual position.
     */
    void setFeedback(const iDynTree::Vector3 &velocity,
                     const iDynTree::Vector3 &position);

    /**
     * Evaluate control
     */
    void evaluateControl() override;
};

#endif
