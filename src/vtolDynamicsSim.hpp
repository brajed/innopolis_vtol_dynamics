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


struct VtolParameters{
    double mass;                                    // kg
    double gravity;                                 // n/sec^2
    double atmoRho;                                 // air density (kg/m^3)
    std::array<double, 5> forceCoeff;
    std::array<double, 5> torqueCoeff;
    double wingArea;                                // m^2
    double characteristicLength;                    // m
    double VmodConversion;                          // m/s
    Eigen::Matrix3d inertia;                        // kg*m^2
    Eigen::Vector3d propellerShift;                 // m

    double dyn_press_conversion;                    // calculate conversion velocity pressure
    std::array<double, 15> propellersLocation;      // calculate

    double massUncertainty;                         // mass multiplier
    double inertiaUncertainty;                      // mass multiplier
};

struct SystemState{
    Eigen::Vector3d eulerAngles;                    // rad
    Eigen::Quaterniond attitude;
    Eigen::Matrix3d rotationIB;                     // inertial CS to body CS
    Eigen::Vector3d angularVelocity;                // rad/sec, in body CS
    Eigen::Vector3d angularAcceleration;            // rad/sec^2, in body CS
    Eigen::Vector3d linearVelocity;                 // m/sec, in body CS
    Eigen::Vector3d linearAcceleration;             // m/sec^2, in body CS
    Eigen::Vector3d windVelocity;                   // m/sec^2
    Eigen::Vector3d gustVelocity;                   // m/sec^2
    Eigen::Vector3d accelBias;
    Eigen::Vector3d gyroBias;
    Eigen::Vector3d Faero;                          // N
    Eigen::Vector3d Maero;                          // N*m
    double Cmx_a;   // coefficients for the jacobian
    double Cmy_e;   // coefficients for the jacobian
    double Cmz_r;   // coefficients for the jacobian
    Eigen::Vector3d Fspecific;                      // N

    Eigen::Quaterniond estdAttitude;
    Eigen::Matrix3d estRotationIB;                  // inertial CS to body CS
    Eigen::Vector3d estAngularVelocity;             // rad/sec, in body CS
    Eigen::Vector3d estAngularAcceleration;         // rad/sec^2, in body CS
    Eigen::Vector3d estLinearVelocity;              // m/sec, in body CS
    Eigen::Vector3d estLinearAcceleration;          // m/sec^2, in body CS

    double windVariance;
    double guVariance;

};

struct CommandedState{
    Eigen::Vector3d eulerAngles;                    // rad
    double AoS;                                     // angle of sideslip, rad
    double AoA;                                     // angle of atack, rad
    double chi;  // what is it?
    double FPA;                                     // flight path angle, rad

    Eigen::Vector3d linearVelocity;                 // m/sec, in body CS
    
    bool controlPhi;
    bool controlTheta;
    bool controlPsi;
    bool controlAoS;
    bool controlAoA;

    double gamma; // weight to prioritize minimization of virtual control allocation error over minimization of actuators deviation

    std::array<double, 8> currentControl;           // rad/sec
    std::array<double, 8> previousControl;          // rad/sec
};

struct SystemConstraints{
    std::array<double, 8> controlMin;               // rad/sec
    std::array<double, 8> controlMax;               // rad/sec
    std::array<double, 8> deltaControlMax;          // rad/sec^2
    std::array<double, 8> timeConstant;             // sec
    std::array<double, 8> desiredControl;           // rad/sec
};

struct Control{
    std::array<double, 6> W_v;                      // virtual control input priority
    std::array<double, 8> W_u;                      // actuator control input priority
    Eigen::Matrix3d K_angle;                        // gain are calculated by using PZP.m
    Eigen::Matrix3d K_omega;                        // gain are calculated by using PZP.m
    Eigen::Matrix3d K_vel;                          // gains are tuned by trial-and-error method
    double K_zeta;                                  // gain is tuned by trial-and-error method
};

struct TablesWithCoeffs{
    Eigen::MatrixXd CS_rudder;
    Eigen::MatrixXd CS_beta;

    Eigen::MatrixXd AoA;
    Eigen::MatrixXd AoS;

    Eigen::MatrixXd actuator;
    Eigen::MatrixXd airspeed;

    Eigen::MatrixXd CLPolynomial;
    Eigen::MatrixXd CSPolynomial;
    Eigen::MatrixXd CDPolynomial;
    Eigen::MatrixXd CmxPolynomial;
    Eigen::MatrixXd CmyPolynomial;
    Eigen::MatrixXd CmzPolynomial;

    Eigen::MatrixXd CmxAileron;
    Eigen::MatrixXd CmyElevator;
    Eigen::MatrixXd CmzRudder;

};

/**
 * @brief Vtol dynamics simulator class
 */
class VtolDynamicsSim{
    public:
        VtolDynamicsSim();
        void init();
        void processStep(double dt_secs,
                         const std::vector<double> & motorSpeedCommandIn,
                         bool isCmdPercent);

        typedef uint64_t Time_t;
        Eigen::Vector3d calculateWind();
        Eigen::Matrix3d calculateRotationMatrix() const;
        Eigen::Vector3d calculateAirSpeed(const Eigen::Matrix3d& rotationMatrix,
                                          const Eigen::Vector3d& estimatedVelocity,
                                          const Eigen::Vector3d& windSpeed);
        double calculateDynamicPressure(double airSpeedMod);
        double calculateAnglesOfAtack(const Eigen::Vector3d& airSpeed) const;
        double calculateAnglesOfSideslip(const Eigen::Vector3d& airSpeed) const;

        void calculateAerodynamics(const Eigen::Vector3d& airspeed,
                                   double dynamicPressure,
                                   double AoA,
                                   double AoS,
                                   double aileron_pos,
                                   double elevator_pos,
                                   double rudder_pos,
                                   Eigen::Vector3d& Faero,
                                   Eigen::Vector3d& Maero,
                                   double& Cmx_a,
                                   double& Cmy_e,
                                   double& Cmz_r);

        void calculateCLPolynomial(double airSpeedMod, Eigen::VectorXd& polynomialCoeffs);
        void calculateCSPolynomial(double airSpeedMod, Eigen::VectorXd& polynomialCoeffs);
        void calculateCDPolynomial(double airSpeedMod, Eigen::VectorXd& polynomialCoeffs);
        void calculateCmxPolynomial(double airSpeedMod, Eigen::VectorXd& polynomialCoeffs);
        void calculateCmyPolynomial(double airSpeedMod, Eigen::VectorXd& polynomialCoeffs);
        void calculateCmzPolynomial(double airSpeedMod, Eigen::VectorXd& polynomialCoeffs);
        void calculatePolynomialUsingTable(const Eigen::MatrixXd& table,
                                           double airSpeedMod,
                                           Eigen::VectorXd& polynomialCoeffs);

        double calculateCSRudder(double rudder_pos, double airspeed) const;
        double calculateCSBeta(double AoS_deg, double airspeed) const;
        double calculateCmxAileron(double aileron_pos, double airspeed) const;
        double calculateCmyElevator(double elevator_pos, double airspeed) const;
        double calculateCmzRudder(double rudder_pos, double airspeed) const;

        size_t findRowForPolynomial(const Eigen::MatrixXd& table, double value) const;
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
        void setEulerAngles(Eigen::Vector3d eulerAngles);
    private:
        void loadTables(const std::string& path);
        void loadParams(const std::string& path);

        VtolParameters params_;
        SystemState sysState_;
        CommandedState cmdState_;
        SystemConstraints sysConstraints_;
        Control control_;
        TablesWithCoeffs tables_;

        std::default_random_engine generator_;
        std::normal_distribution<double> distribution_;
};

#endif // VTOL_DYNAMICS_SIM_H