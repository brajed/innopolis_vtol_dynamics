/**
 * @file vtolDynamicsSim.hpp
 * @author ponomarevda96@gmail.com
 * @brief Vtol dynamics simulator class header file
 */

#ifndef VTOL_DYNAMICS_SIM_H
#define VTOL_DYNAMICS_SIM_H

#include <Eigen/Geometry>
#include <vector>
#include <array>
#include <random>
#include "uavDynamicsSimBase.hpp"


struct VtolParameters{
    double mass;                                    // kg
    double gravity;                                 // n/sec^2
    double atmoRho;                                 // air density (kg/m^3)
    double wingArea;                                // m^2
    double characteristicLength;                    // m
    Eigen::Matrix<double, 3, 3, Eigen::RowMajor> inertia;   // kg*m^2

    std::array<Eigen::Vector3d, 5> propellersLocation;

    std::vector<double> actuatorMin;                // rad/sec
    std::vector<double> actuatorMax;                // rad/sec
    std::array<double, 8> deltaControlMax;          // rad/sec^2
    std::array<double, 8> timeConstant;             // sec

    double accVariance;
    double gyroVariance;

    /**
     * @note not ready yet
     */
    double massUncertainty;                         // multiplier
    double inertiaUncertainty;                      // multiplier
};

struct State{
    /**
     * @note Inertial frame (NED)
     */
    Eigen::Vector3d initialPose;                    // meters
    Eigen::Vector3d position;                       // meters
    Eigen::Vector3d linearVel;                      // m/sec
    Eigen::Vector3d linearAccel;                    // m/sec^2

    /**
     * @note Body frame (FRD)
     */
    Eigen::Quaterniond initialAttitude;             // quaternion
    Eigen::Quaterniond attitude;                    // quaternion
    Eigen::Vector3d angularVel;                     // rad/sec
    Eigen::Vector3d angularAccel;                   // rad/sec^2

    Eigen::Vector3d Flift;                          // N
    Eigen::Vector3d Fdrug;                          // N
    Eigen::Vector3d Fside;                          // N
    Eigen::Vector3d Faero;                          // N
    Eigen::Vector3d Maero;                          // N*m
    Eigen::Vector3d Msteer;                         // N*m
    Eigen::Vector3d Mairspeed;                      // N*m
    Eigen::Vector3d MmotorsTotal;                   // N*m
    std::array<Eigen::Vector3d, 5> Fmotors;         // N
    std::array<Eigen::Vector3d, 5> Mmotors;         // N*m
    std::array<double, 5> motorsRpm;                // rpm
    Eigen::Vector3d Fspecific;                      // N
    Eigen::Vector3d Ftotal;                         // N
    Eigen::Vector3d Mtotal;                         // N*m
    Eigen::Vector3d bodylinearVel;                  // m/sec (just for debug only)

    /**
     * @note parameters
     */
    Eigen::Vector3d accelBias;
    Eigen::Vector3d gyroBias;
    double windVariance;

    /**
     * @note not ready yet
     */
    Eigen::Vector3d windVelocity;                   // m/sec^2
    Eigen::Vector3d gustVelocity;                   // m/sec^2
    double gustVariance;
    std::vector<double> prevActuators;              // rad/sec
    std::vector<double> crntActuators;              // rad/sec
};

struct TablesWithCoeffs{
    Eigen::Matrix<double, 8, 20, Eigen::RowMajor> CS_rudder;
    Eigen::Matrix<double, 8, 90, Eigen::RowMajor> CS_beta;

    Eigen::Matrix<double, 1, 47, Eigen::RowMajor> AoA;
    Eigen::Matrix<double, 90, 1, Eigen::ColMajor> AoS;

    Eigen::Matrix<double, 20, 1, Eigen::ColMajor> actuator;
    Eigen::Matrix<double, 8, 1, Eigen::ColMajor> airspeed;

    Eigen::Matrix<double, 8, 8, Eigen::RowMajor> CLPolynomial;
    Eigen::Matrix<double, 8, 8, Eigen::RowMajor> CSPolynomial;
    Eigen::Matrix<double, 8, 6, Eigen::RowMajor> CDPolynomial;
    Eigen::Matrix<double, 8, 8, Eigen::RowMajor> CmxPolynomial;
    Eigen::Matrix<double, 8, 8, Eigen::RowMajor> CmyPolynomial;
    Eigen::Matrix<double, 8, 8, Eigen::RowMajor> CmzPolynomial;

    Eigen::Matrix<double, 8, 20, Eigen::RowMajor> CmxAileron;
    Eigen::Matrix<double, 8, 20, Eigen::RowMajor> CmyElevator;
    Eigen::Matrix<double, 8, 20, Eigen::RowMajor> CmzRudder;

    Eigen::Matrix<double, 40, 5, Eigen::RowMajor> prop;

    std::vector<double> actuatorTimeConstants;
};

/**
 * @brief Vtol dynamics simulator class
 */
class InnoVtolDynamicsSim : public UavDynamicsSimBase{
    public:
        InnoVtolDynamicsSim();
        virtual int8_t init() override;
        virtual void setInitialPosition(const Eigen::Vector3d & position,
                                        const Eigen::Quaterniond& attitude) override;
        virtual void land() override;
        virtual int8_t calibrate(CalibrationType_t calibrationType) override;
        virtual void process(double dt_secs,
                             const std::vector<double>& motorSpeedCommandIn,
                             bool isCmdPercent) override;

        /**
         * @note These methods should return in ned format
         */
        virtual Eigen::Vector3d getVehiclePosition() const override;
        virtual Eigen::Quaterniond getVehicleAttitude() const override;
        virtual Eigen::Vector3d getVehicleVelocity() const override;
        virtual Eigen::Vector3d getVehicleAngularVelocity() const override;
        virtual void getIMUMeasurement(Eigen::Vector3d& accOut, Eigen::Vector3d& gyroOut) override;
        virtual bool getMotorsRpm(std::vector<double>& motorsRpm) override;

        /**
         * @note These methods should be public for debug only (publish to ros topic)
         * it's better to refactor them
         */
        Eigen::Vector3d getFaero() const;
        Eigen::Vector3d getFtotal() const;
        Eigen::Vector3d getMaero() const;
        Eigen::Vector3d getMtotal() const;
        Eigen::Vector3d getAngularAcceleration() const;
        Eigen::Vector3d getLinearAcceleration() const;
        Eigen::Vector3d getMsteer() const;
        Eigen::Vector3d getMairspeed() const;
        Eigen::Vector3d getMmotorsTotal() const;
        Eigen::Vector3d getBodyLinearVelocity() const;
        Eigen::Vector3d getFlift() const;
        Eigen::Vector3d getFdrug() const;
        Eigen::Vector3d getFside() const;
        const std::array<Eigen::Vector3d, 5>& getFmotors() const;
        const std::array<Eigen::Vector3d, 5>& getMmotors() const;

        /**
         * @note The methods below are should be public for test only
         * think about making test as friend
         */
        Eigen::Vector3d calculateNormalForceWithoutMass();
        Eigen::Vector3d calculateWind();
        Eigen::Matrix3d calculateRotationMatrix() const;
        double calculateDynamicPressure(double airSpeedMod);
        double calculateAnglesOfAtack(const Eigen::Vector3d& airSpeed) const;
        double calculateAnglesOfSideslip(const Eigen::Vector3d& airSpeed) const;
        void thruster(double actuator, double& thrust, double& torque, double& rpm) const;
        void calculateNewState(const Eigen::Vector3d& Maero,
                               const Eigen::Vector3d& Faero,
                               const std::vector<double>& actuator,
                               double dt_sec);

        void calculateAerodynamics(const Eigen::Vector3d& airspeed,
                                   double AoA,
                                   double AoS,
                                   double aileron_pos,
                                   double elevator_pos,
                                   double rudder_pos,
                                   Eigen::Vector3d& Faero,
                                   Eigen::Vector3d& Maero);

        void calculateCLPolynomial(double airSpeedMod, Eigen::VectorXd& polynomialCoeffs) const;
        void calculateCSPolynomial(double airSpeedMod, Eigen::VectorXd& polynomialCoeffs) const;
        void calculateCDPolynomial(double airSpeedMod, Eigen::VectorXd& polynomialCoeffs) const;
        void calculateCmxPolynomial(double airSpeedMod, Eigen::VectorXd& polynomialCoeffs) const;
        void calculateCmyPolynomial(double airSpeedMod, Eigen::VectorXd& polynomialCoeffs) const;
        void calculateCmzPolynomial(double airSpeedMod, Eigen::VectorXd& polynomialCoeffs) const;
        void calculatePolynomialUsingTable(const Eigen::MatrixXd& table,
                                           double airSpeedMod,
                                           Eigen::VectorXd& polynomialCoeffs) const;

        double calculateCSRudder(double rudder_pos, double airspeed) const;
        double calculateCSBeta(double AoS_deg, double airspeed) const;
        double calculateCmxAileron(double aileron_pos, double airspeed) const;
        double calculateCmyElevator(double elevator_pos, double airspeed) const;
        double calculateCmzRudder(double rudder_pos, double airspeed) const;

        size_t findRow(const Eigen::MatrixXd& table, double value) const;
        double lerp(double a, double b, double f) const;
        /**
         * @note Similar to https://www.mathworks.com/help/matlab/ref/griddata.html
         * Implementation from https://en.wikipedia.org/wiki/Bilinear_interpolation
         */
        double griddata(const Eigen::MatrixXd& x,
                        const Eigen::MatrixXd& y,
                        const Eigen::MatrixXd& z,
                        double xi,
                        double yi) const;
        double polyval(const Eigen::VectorXd& poly, double val) const;
        size_t search(const Eigen::MatrixXd& matrix, double key) const;


        void setWindParameter(Eigen::Vector3d windMeanVelocity, double wind_velocityVariance);
        void setInitialVelocity(const Eigen::Vector3d& linearVelocity,
                                const Eigen::Vector3d& angularVelocity);

    private:
        void loadTables(const std::string& path);
        void loadParams(const std::string& path);
        std::vector<double> mapCmdToActuatorStandardVTOL(const std::vector<double>& cmd) const;
        std::vector<double> mapCmdToActuatorInnoVTOL(const std::vector<double>& cmd) const;
        void updateActuators(std::vector<double>& cmd, double dtSecs);
        Eigen::Vector3d calculateAirSpeed(const Eigen::Matrix3d& rotationMatrix,
                                    const Eigen::Vector3d& estimatedVelocity,
                                    const Eigen::Vector3d& windSpeed) const;

        VtolParameters params_;
        State state_;
        TablesWithCoeffs tables_;

        std::default_random_engine generator_;
        std::normal_distribution<double> distribution_;
};

#endif  // VTOL_DYNAMICS_SIM_H
