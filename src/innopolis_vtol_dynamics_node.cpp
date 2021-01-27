/**
 * @file innopolis_vtol_dynamics_node.cpp
 * @author Dmitry Ponomarev
 * @author Roman Fedorenko
 * @author Ezra Tal
 * @author Winter Guerra
 * @author Varun Murali
 * @brief Implementation of UAV dynamics, IMU, and angular rate control simulation node
 */

#include <rosgraph_msgs/Clock.h>
#include <visualization_msgs/Marker.h>
#include <geometry_msgs/TransformStamped.h>
#include <geometry_msgs/TwistStamped.h>
#include <geometry_msgs/Twist.h>
#include <geometry_msgs/QuaternionStamped.h>
#include <std_msgs/Float64MultiArray.h>
#include <sensor_msgs/NavSatFix.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/MagneticField.h>
#include <innopolis_vtol_dynamics/RawAirData.h>
#include <innopolis_vtol_dynamics/StaticPressure.h>
#include <innopolis_vtol_dynamics/StaticTemperature.h>

#include "innopolis_vtol_dynamics_node.hpp"
#include "flightgogglesDynamicsSim.hpp"
#include "vtolDynamicsSim.hpp"
#include "cs_converter.hpp"


static char GLOBAL_FRAME_ID[] = "world";
static char UAV_FRAME_ID[] = "/uav/enu";


int main(int argc, char **argv){
    ros::init(argc, argv, "innopolis_vtol_dynamics_node");
    if( ros::console::set_logger_level(ROSCONSOLE_DEFAULT_NAME, ros::console::levels::Debug) ) {
        ros::console::notifyLoggerLevelsChanged();
    }
    ros::NodeHandle node_handler;
    Uav_Dynamics uav_dynamics_node(node_handler);
    if(uav_dynamics_node.init() == -1){
        ROS_ERROR("Shutdown.");
        ros::shutdown();
        return 0;
    }
    ros::spin();
    return 0;
}


Uav_Dynamics::Uav_Dynamics(ros::NodeHandle nh): node_(nh), actuators_(8, 0.){
}


/**
 * @return -1 if error occured, else 0
 */
int8_t Uav_Dynamics::init(){
    // Get Simulator parameters
    std::vector<double> initPose(7);
    std::string dynamics_type, vehicle;
    const std::string SIM_PARAMS_PATH = "/uav/sim_params/";
    if(!ros::param::get(SIM_PARAMS_PATH + "use_sim_time",       useSimTime_ ) ||
       !ros::param::get(SIM_PARAMS_PATH + "lat_ref",            latRef_) ||
       !ros::param::get(SIM_PARAMS_PATH + "lon_ref",            lonRef_) ||
       !ros::param::get(SIM_PARAMS_PATH + "alt_ref",            altRef_) ||
       !ros::param::get(SIM_PARAMS_PATH + "dynamics_type",      dynamics_type) ||
       !node_.getParam("/inno_dynamics_sim/vehicle",            vehicle) ||
       !ros::param::get(SIM_PARAMS_PATH + "init_pose",          initPose)){
        ROS_ERROR("Dynamics: There is no at least one of required simulator parameters.");
        return -1;
    }
    /**
     * @brief Init dynamics simulator
     * @todo it's better to use some build method instead of manually call new
     */
    if(dynamics_type == "flightgoggles_multicopter"){
        dynamicsType_ = FLIGHTGOGGLES_MULTICOPTER;
        uavDynamicsSim_ = new FlightgogglesDynamics;
    }else if(dynamics_type == "inno_vtol"){
        uavDynamicsSim_ = new InnoVtolDynamicsSim;
        dynamicsType_ = INNO_VTOL;
    }else{
        ROS_ERROR("Dynamics type with name \"%s\" is not exist.", dynamics_type.c_str());
        return -1;
    }
    if(vehicle == "standard_vtol"){
        airframeType_ = STANDARD_VTOL;
    }else if(vehicle == "iris"){
        airframeType_ = IRIS;
    }else{
        ROS_ERROR("Wrong vehicle. It should be 'standard_vtol' or 'iris'");
        return -1;
    }

    if(uavDynamicsSim_ == nullptr || uavDynamicsSim_->init() == -1){
        ROS_ERROR("Can't init uav dynamics sim. Shutdown.");
        return -1;
    }
    uavDynamicsSim_->initStaticMotorTransform();
    uavDynamicsSim_->setReferencePosition(latRef_, lonRef_, altRef_);
    geodeticConverter_.initialiseReference(latRef_, lonRef_, altRef_);

    Eigen::Vector3d initPosition(initPose.at(0), initPose.at(1), initPose.at(2));
    Eigen::Quaterniond initAttitude(initPose.at(6), initPose.at(3), initPose.at(4), initPose.at(5));
    initAttitude.normalize();
    uavDynamicsSim_->setInitialPosition(initPosition, initAttitude);

    // Topics name:
    constexpr char IMU_TOPIC_NAME[]                 = "/uav/imu";
    constexpr char MAG_TOPIC_NAME[]                 = "/uav/mag";
    constexpr char GPS_POSE_TOPIC_NAME[]            = "/uav/gps_position";
    constexpr char ATTITUDE_TOPIC_NAME[]            = "/uav/attitude";
    constexpr char VELOCITY_TOPIC_NAME[]            = "/uav/velocity";
    constexpr char ACTUATOR_TOPIC_NAME[]            = "/uav/actuators";
    constexpr char ARM_TOPIC_NAME[]                 = "/uav/arm";

    constexpr char RAW_AIR_DATA_TOPIC_NAME[]        = "/uav/raw_air_data";
    constexpr char STATIC_TEMPERATURE_TOPIC_NAME[]  = "/uav/static_temperature";
    constexpr char STATIC_PRESSURE_TOPIC_NAME[]     = "/uav/static_pressure";

    // Init subscribers and publishers for communicator
    // Imu should has up to 100ms sim time buffer (see issue #63)
    actuatorsSub_ = node_.subscribe(ACTUATOR_TOPIC_NAME, 1, &Uav_Dynamics::actuatorsCallback, this);
    armSub_ = node_.subscribe(ARM_TOPIC_NAME, 1, &Uav_Dynamics::armCallback, this);

    imuPub_ = node_.advertise<sensor_msgs::Imu>(IMU_TOPIC_NAME, 96);
    gpsPositionPub_ = node_.advertise<sensor_msgs::NavSatFix>(GPS_POSE_TOPIC_NAME, 1);
    attitudePub_ = node_.advertise<geometry_msgs::QuaternionStamped>(ATTITUDE_TOPIC_NAME, 1);
    speedPub_ = node_.advertise<geometry_msgs::Twist>(VELOCITY_TOPIC_NAME, 1);
    magPub_ = node_.advertise<sensor_msgs::MagneticField>(MAG_TOPIC_NAME, 1);

    rawAirDataPub_ = node_.advertise<innopolis_vtol_dynamics::RawAirData>(RAW_AIR_DATA_TOPIC_NAME, 1);
    staticTemperaturePub_ = node_.advertise<innopolis_vtol_dynamics::StaticTemperature>(STATIC_TEMPERATURE_TOPIC_NAME, 1);
    staticPressurePub_ = node_.advertise<innopolis_vtol_dynamics::StaticPressure>(STATIC_PRESSURE_TOPIC_NAME, 1);

    // Calibration
    calibrationSub_ = node_.subscribe("/uav/calibration", 1, &Uav_Dynamics::calibrationCallback, this);

    // Other publishers and subscribers
    forcesPub_ = node_.advertise<std_msgs::Float64MultiArray>("/uav/threads_info", 1);

    totalForcePub_ = node_.advertise<visualization_msgs::Marker>("/uav/Ftotal", 1);
    aeroForcePub_ = node_.advertise<visualization_msgs::Marker>("/uav/Faero", 1);
    motorsForcesPub_[0] = node_.advertise<visualization_msgs::Marker>("/uav/Fmotor0", 1);
    motorsForcesPub_[1] = node_.advertise<visualization_msgs::Marker>("/uav/Fmotor1", 1);
    motorsForcesPub_[2] = node_.advertise<visualization_msgs::Marker>("/uav/Fmotor2", 1);
    motorsForcesPub_[3] = node_.advertise<visualization_msgs::Marker>("/uav/Fmotor3", 1);
    motorsForcesPub_[4] = node_.advertise<visualization_msgs::Marker>("/uav/Fmotor4", 1);
    liftForcePub_ = node_.advertise<visualization_msgs::Marker>("/uav/liftForce", 1);
    drugForcePub_ = node_.advertise<visualization_msgs::Marker>("/uav/drugForce", 1);
    sideForcePub_ = node_.advertise<visualization_msgs::Marker>("/uav/sideForce", 1);

    totalMomentPub_ = node_.advertise<visualization_msgs::Marker>("/uav/Mtotal", 1);
    aeroMomentPub_ = node_.advertise<visualization_msgs::Marker>("/uav/Maero", 1);
    motorsMomentsPub_[0] = node_.advertise<visualization_msgs::Marker>("/uav/Mmotor0", 1);
    motorsMomentsPub_[1] = node_.advertise<visualization_msgs::Marker>("/uav/Mmotor1", 1);
    motorsMomentsPub_[2] = node_.advertise<visualization_msgs::Marker>("/uav/Mmotor2", 1);
    motorsMomentsPub_[3] = node_.advertise<visualization_msgs::Marker>("/uav/Mmotor3", 1);
    motorsMomentsPub_[4] = node_.advertise<visualization_msgs::Marker>("/uavM/motor4", 1);

    velocityPub_ = node_.advertise<visualization_msgs::Marker>("/uav/linearVelocity", 1);

    if(useSimTime_){
        clockPub_ = node_.advertise<rosgraph_msgs::Clock>("/clock", 1);
        clockPub_.publish(currentTime_);
    }else{
        // Get the current time if we are using wall time. Otherwise, use 0 as initial clock.
        currentTime_ = ros::Time::now();
    }

    simulationLoopTimer_ = node_.createWallTimer(ros::WallDuration(dt_secs_/clockScale_),
                                                 &Uav_Dynamics::simulationLoopTimerCallback,
                                                 this);
    simulationLoopTimer_.start();

    proceedDynamicsTask = std::thread(&Uav_Dynamics::proceedQuadcopterDynamics, this, dt_secs_);
    proceedDynamicsTask.detach();

    publishToRosTask = std::thread(&Uav_Dynamics::publishToRos, this, ROS_PUB_PERIOD_SEC);
    publishToRosTask.detach();

    diagnosticTask = std::thread(&Uav_Dynamics::performDiagnostic, this, 1.0);
    diagnosticTask.detach();


    return 0;
}


/**
 * @brief Main Simulator loop
 * @param event Wall clock timer event
 */
void Uav_Dynamics::simulationLoopTimerCallback(const ros::WallTimerEvent& event){
    if (useSimTime_){
        currentTime_ += ros::Duration(dt_secs_);
        clockPub_.publish(currentTime_);
    } else {
        ros::Time loopStartTime = ros::Time::now();
        dt_secs_ = (loopStartTime - currentTime_).toSec();
        currentTime_ = loopStartTime;
    }
}


void Uav_Dynamics::performDiagnostic(double periodSec){
    while(ros::ok()){
        auto crnt_time = std::chrono::system_clock::now();
        auto sleed_period = std::chrono::seconds(int(periodSec * clockScale_));

        // Monitor thread and ros topic frequency
        float dynamicsCompleteness = dynamicsCounter_ * dt_secs_ / (clockScale_ * periodSec);
        float rosPubCompleteness = rosPubCounter_ * ROS_PUB_PERIOD_SEC / (clockScale_ * periodSec);
        std::stringstream diagnosticStream;
        diagnosticStream << "time elapsed: " << periodSec << " secs. ";
        if(dynamicsCompleteness < 0.9){
            diagnosticStream << "\033[1;31mdyn=" << dynamicsCompleteness << "\033[0m, ";
        }else{
            diagnosticStream << "dyn=" << dynamicsCompleteness << ", ";
        }
        if(dynamicsCompleteness < 0.9){
            diagnosticStream << "\033[1;31mros_pub=" << rosPubCompleteness << "\033[0m, ";
        }else{
            diagnosticStream << "ros_pub=" << rosPubCompleteness << ", ";
        }
        if(actuatorsMsgCounter_ < 100 || maxDelayUsec_ > 20000 || maxDelayUsec_ == 0){
            diagnosticStream << "\033[1;31mactuators_recv=" << actuatorsMsgCounter_ << "times"
                          << "/" << maxDelayUsec_ << "\033[0m us.";
        }else{
            diagnosticStream << "actuators_recv=" << actuatorsMsgCounter_ << "times"
                          << "/" << maxDelayUsec_ << " us.";
        }
        dynamicsCounter_ = 0;
        rosPubCounter_ = 0;
        actuatorsMsgCounter_ = 0;
        maxDelayUsec_ = 0;
        ROS_INFO_STREAM("InnoDynamicsSim:" << diagnosticStream.str());

        auto enuPosition = uavDynamicsSim_->getVehiclePosition();
        ROS_INFO("InnoDynamicsSim: \033[1;29m mc \033[0m [%.2f, %.2f, %.2f, %.2f], \033[1;29m fw rpy \033[0m [%.2f, %.2f, %.2f] \033[1;29m throttle \033[0m [%.2f], \033[1;29m ned pose \033[0m [%.1f, %.1f, %.1f]",
            actuators_[0], actuators_[1], actuators_[2], actuators_[3],
            actuators_[4], actuators_[5], actuators_[6],
            actuators_[7],
            enuPosition[0], enuPosition[1], enuPosition[2]);
        std::this_thread::sleep_until(crnt_time + sleed_period);
    }
}

// The sequence of steps for lockstep are:
// The simulation sends a sensor message HIL_SENSOR including a timestamp time_usec to update
// the sensor state and time of PX4.
// PX4 receives this and does one iteration of state estimation, controls, etc. and eventually
// sends an actuator message HIL_ACTUATOR_CONTROLS.
// The simulation waits until it receives the actuator/motor message, then simulates the physics
// and calculates the next sensor message to send to PX4 again.
// The system starts with a "freewheeling" period where the simulation sends sensor messages
// including time and therefore runs PX4 until it has initialized and responds with an actautor
// message.
// But instead of waiting actuators cmd, we will wait for an arming
void Uav_Dynamics::proceedQuadcopterDynamics(double period){
    while(ros::ok()){
        auto crnt_time = std::chrono::system_clock::now();
        auto sleed_period = std::chrono::milliseconds(int(1000 * period * clockScale_));
        auto time_point = crnt_time + sleed_period;
        dynamicsCounter_++;

        if(calibrationType_ != UavDynamicsSimBase::CalibrationType_t::WORK_MODE){
            uavDynamicsSim_->calibrate(calibrationType_);
        }else if(armed_){
            static auto crnt_time = std::chrono::system_clock::now();
            auto prev_time = crnt_time;
            crnt_time = std::chrono::system_clock::now();
            auto time_dif_sec = (crnt_time - prev_time).count() / 1000000000.0;

            uavDynamicsSim_->process(time_dif_sec, actuators_, true);
        }else{
            uavDynamicsSim_->land();
        }

        publishStateToCommunicator();

        std::this_thread::sleep_until(time_point);
    }
}

void Uav_Dynamics::publishStateToCommunicator(){
    // Get state in ROS notation
    auto enuPosition = uavDynamicsSim_->getVehiclePosition();
    auto attitudeFluToEnu = uavDynamicsSim_->getVehicleAttitude();
    Eigen::Vector3d accFlu(0, 0, -9.8), gyroFlu(0, 0, 0);
    uavDynamicsSim_->getIMUMeasurement(accFlu, gyroFlu);
    auto linVelEnu = uavDynamicsSim_->getVehicleVelocity();
    auto angVelFlu = uavDynamicsSim_->getVehicleAngularVelocity();

    // Convert state from ROS notation into PX4
    Eigen::Vector3d gpsPosition;
    geodeticConverter_.enu2Geodetic(enuPosition[0], enuPosition[1], enuPosition[2],
                                    &gpsPosition[0], &gpsPosition[1], &gpsPosition[2]);

    auto accFrd = Converter::fluToFrd(accFlu);
    auto gyroFrd = Converter::fluToFrd(gyroFlu);

    auto linVelNed = Converter::enuToNed(linVelEnu);

    // Calculate temperature and pressure and density using ISA model
    const float PRESSURE_MSL_HPA = 1013.250f;
    const float TEMPERATURE_MSL_KELVIN = 288.0f;
    const float RHO_MSL = 1.225f;

    const float LAPSE_TEMPERATURE_RATE = 1 / 152.4; // 0.00656

    float alt_msl = gpsPosition.z();

    float temperatureLocalKelvin = TEMPERATURE_MSL_KELVIN - LAPSE_TEMPERATURE_RATE * alt_msl;
    float pressureRatio = powf((TEMPERATURE_MSL_KELVIN/temperatureLocalKelvin), 5.256f);
    const float densityRatio = powf((TEMPERATURE_MSL_KELVIN/temperatureLocalKelvin), 4.256f);
    float rho = RHO_MSL / densityRatio;
    float absPressure = PRESSURE_MSL_HPA / pressureRatio;
    float diffPressure = 0.005f * rho * linVelNed.norm() * linVelNed.norm();

    // Publish state to communicator
    auto crntTimeSec = currentTime_.toSec();
    if(gpsLastPubTimeSec_ + GPS_POSITION_PERIOD < crntTimeSec){
        publishUavGpsPosition(gpsPosition);
        gpsLastPubTimeSec_ = crntTimeSec;
    }
    if(attitudeLastPubTimeSec_ + ATTITUDE_PERIOD < crntTimeSec){
        publishUavAttitude(attitudeFluToEnu);
        attitudeLastPubTimeSec_ = crntTimeSec;
    }
    if(velocityLastPubTimeSec_ + VELOCITY_PERIOD < crntTimeSec){
        publishUavVelocity(linVelNed, angVelFlu);
        velocityLastPubTimeSec_ = crntTimeSec;
    }
    if(imuLastPubTimeSec_ + IMU_PERIOD < crntTimeSec){
        publishIMUMeasurement(accFrd, gyroFrd);
        imuLastPubTimeSec_ = crntTimeSec;
    }
    if(magLastPubTimeSec_ + MAG_PERIOD < crntTimeSec){
        publishUavMag(gpsPosition, attitudeFluToEnu);
        magLastPubTimeSec_ = crntTimeSec;
    }
    if(rawAirDataLastPubTimeSec_ + RAW_AIR_DATA_PERIOD < crntTimeSec){
        publishUavAirData(absPressure, diffPressure);
        rawAirDataLastPubTimeSec_ = crntTimeSec;
    }
    if(staticPressureLastPubTimeSec_ + STATIC_PRESSURE_PERIOD < crntTimeSec){
        publishUavStaticPressure(absPressure);
        staticPressureLastPubTimeSec_ = crntTimeSec;
    }
    if(staticTemperatureLastPubTimeSec_ + STATIC_TEMPERATURE_PERIOD < crntTimeSec){
        publishUavStaticTemperature(temperatureLocalKelvin);
        staticTemperatureLastPubTimeSec_ = crntTimeSec;
    }
}

void Uav_Dynamics::publishToRos(double period){
    while(ros::ok()){
        auto crnt_time = std::chrono::system_clock::now();
        auto sleed_period = std::chrono::microseconds(int(1000000 * period * clockScale_));
        auto time_point = crnt_time + sleed_period;
        rosPubCounter_++;

        publishState();

        static auto next_time = std::chrono::system_clock::now();
        if(crnt_time > next_time){
            publishForcesAndMomentsInfo();
            next_time += std::chrono::milliseconds(int(50));
        }

        std::this_thread::sleep_until(time_point);
    }
}

void Uav_Dynamics::actuatorsCallback(sensor_msgs::Joy::Ptr msg){
    prevActuatorsTimestampUsec_ = lastActuatorsTimestampUsec_;
    lastActuatorsTimestampUsec_ = msg->header.stamp.toNSec() / 1000;
    auto crntDelayUsec = lastActuatorsTimestampUsec_ - prevActuatorsTimestampUsec_;
    if(crntDelayUsec > maxDelayUsec_){
        maxDelayUsec_ = crntDelayUsec;
    }
    actuatorsMsgCounter_++;

    for(size_t idx = 0; idx < msg->axes.size(); idx++){
        actuators_[idx] = msg->axes[idx];
    }
}

void Uav_Dynamics::armCallback(std_msgs::Bool msg){
    if(armed_ != msg.data){
        /**
         * @note why it publish few times when sim starts? hack: use throttle
         */
        ROS_INFO_STREAM_THROTTLE(1, "cmd: " << (msg.data ? "Arm" : "Disarm"));
    }
    armed_ = msg.data;
}

void Uav_Dynamics::calibrationCallback(std_msgs::UInt8 msg){
    if(calibrationType_ != msg.data){
        ROS_INFO_STREAM_THROTTLE(1, "calibration type: " << msg.data + 0);
    }
    calibrationType_ = msg.data;
}

void Uav_Dynamics::publishState(void){
    geometry_msgs::TransformStamped transform;

    transform.header.stamp = ros::Time::now();
    transform.header.frame_id = "world";

    auto enuPosition = uavDynamicsSim_->getVehiclePosition();
    auto fluAttitude = uavDynamicsSim_->getVehicleAttitude();

    transform.transform.translation.x = enuPosition(0);
    transform.transform.translation.y = enuPosition(1);
    transform.transform.translation.z = enuPosition(2);
    transform.transform.rotation.x = fluAttitude.x();
    transform.transform.rotation.y = fluAttitude.y();
    transform.transform.rotation.z = fluAttitude.z();
    transform.transform.rotation.w = fluAttitude.w();
    transform.child_frame_id = UAV_FRAME_ID;

    tfPub_.sendTransform(transform);

    transform.header.frame_id = GLOBAL_FRAME_ID;
    transform.transform.rotation.x = 0;
    transform.transform.rotation.y = 0;
    transform.transform.rotation.z = 0;
    transform.transform.rotation.w = 1;
    transform.child_frame_id = "uav/com";
    tfPub_.sendTransform(transform);
}

void Uav_Dynamics::publishUavAttitude(Eigen::Quaterniond attitudeFrdToNed){
    geometry_msgs::QuaternionStamped msg;
    msg.quaternion.x = attitudeFrdToNed.x();
    msg.quaternion.y = attitudeFrdToNed.y();
    msg.quaternion.z = attitudeFrdToNed.z();
    msg.quaternion.w = attitudeFrdToNed.w();
    msg.header.stamp = currentTime_;
    attitudePub_.publish(msg);
}

void Uav_Dynamics::publishUavGpsPosition(Eigen::Vector3d geoPosition){
    sensor_msgs::NavSatFix gps_pose;
    gps_pose.latitude = geoPosition[0];
    gps_pose.longitude = geoPosition[1];
    gps_pose.altitude = geoPosition[2];
    gps_pose.header.stamp = currentTime_;
    gpsPositionPub_.publish(gps_pose);
}

void Uav_Dynamics::publishIMUMeasurement(Eigen::Vector3d accFrd, Eigen::Vector3d gyroFrd){
    sensor_msgs::Imu msg;
    msg.header.stamp = currentTime_;

    msg.angular_velocity.x = gyroFrd[0];
    msg.angular_velocity.y = gyroFrd[1];
    msg.angular_velocity.z = gyroFrd[2];

    msg.linear_acceleration.x = accFrd[0];
    msg.linear_acceleration.y = accFrd[1];
    msg.linear_acceleration.z = accFrd[2];

    imuPub_.publish(msg);
}

void Uav_Dynamics::publishUavVelocity(Eigen::Vector3d linVelNed, Eigen::Vector3d angVelFrd){
    geometry_msgs::Twist speed;
    speed.linear.x = linVelNed[0];
    speed.linear.y = linVelNed[1];
    speed.linear.z = linVelNed[2];
    speed.angular.x = angVelFrd[0];
    speed.angular.y = angVelFrd[1];
    speed.angular.z = angVelFrd[2];
    speedPub_.publish(speed);
}

void Uav_Dynamics::publishUavMag(Eigen::Vector3d geoPosition, Eigen::Quaterniond attitudeFluToNed){
    Eigen::Vector3d magEnu;
    geographiclib_conversions::MagneticField(
        geoPosition.x(), geoPosition.y(), geoPosition.z(),
        magEnu.x(), magEnu.y(), magEnu.z());

    // there should be some mistake, is not it?
    // if we really want frd, we actually need to multiple
    // but in this situation YAW (and may smth) is inversed
    static const auto Q_FLU_TO_FRD = Eigen::Quaterniond(0, 1, 0, 0);
    // Eigen::Vector3d magFrd = Q_FLU_TO_FRD * (attitudeFluToNed.inverse() * magEnu);
    Eigen::Vector3d magFrd = (attitudeFluToNed.inverse() * magEnu);

    sensor_msgs::MagneticField mag;
    mag.header.stamp = ros::Time();
    mag.magnetic_field.x = magFrd[0];
    mag.magnetic_field.y = magFrd[1];
    mag.magnetic_field.z = magFrd[2];
    magPub_.publish(mag);
}

void Uav_Dynamics::publishUavAirData(float absPressure, float diffPressure){
    innopolis_vtol_dynamics::RawAirData msg;
    msg.header.stamp = ros::Time();
    msg.static_pressure = absPressure;
    msg.differential_pressure = diffPressure;
    rawAirDataPub_.publish(msg);
}

void Uav_Dynamics::publishUavStaticTemperature(float staticTemperature){
    innopolis_vtol_dynamics::StaticTemperature msg;
    msg.header.stamp = ros::Time();
    msg.static_temperature = staticTemperature;
    staticTemperaturePub_.publish(msg);
}

void Uav_Dynamics::publishUavStaticPressure(float staticPressure){
    innopolis_vtol_dynamics::StaticPressure msg;
    msg.header.stamp = ros::Time();
    msg.static_pressure = staticPressure;
    staticPressurePub_.publish(msg);
}

void fillArrow(visualization_msgs::Marker& arrow,
               const Eigen::Vector3d& vector3D,
               const Eigen::Vector3d& rgbColor){
    arrow.points[1].x = vector3D[0];
    arrow.points[1].y = vector3D[1];
    arrow.points[1].z = vector3D[2];
    arrow.color.r = rgbColor[0];
    arrow.color.g = rgbColor[1];
    arrow.color.b = rgbColor[2];
}

/**
 * @brief Publish forces and moments of vehicle
 */
void Uav_Dynamics::publishForcesAndMomentsInfo(void){
    std_msgs::Float64MultiArray forces;
    forces.data.resize(21);
    forces.data[0] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getFaero()[0];
    forces.data[1] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getFaero()[1];
    forces.data[2] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getFaero()[2];

    forces.data[3] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getFtotal()[0];
    forces.data[4] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getFtotal()[1];
    forces.data[5] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getFtotal()[2];

    forces.data[6] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getMaero()[0];
    forces.data[7] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getMaero()[1];
    forces.data[8] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getMaero()[2];

    forces.data[9] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getMtotal()[0];
    forces.data[10] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getMtotal()[1];
    forces.data[11] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getMtotal()[2];

    forces.data[12] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getMsteer()[0];
    forces.data[13] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getMsteer()[1];
    forces.data[14] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getMsteer()[2];

    forces.data[15] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getMairspeed()[0];
    forces.data[16] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getMairspeed()[1];
    forces.data[17] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getMairspeed()[2];

    forces.data[18] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getMmotorsTotal()[0];
    forces.data[19] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getMmotorsTotal()[1];
    forces.data[20] = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getMmotorsTotal()[2];
    forcesPub_.publish(forces);

    if(dynamicsType_ == INNO_VTOL){
        visualization_msgs::Marker arrow;
        arrow.header.frame_id = UAV_FRAME_ID;
        arrow.header.stamp = ros::Time();
        arrow.id = 0;
        arrow.type = visualization_msgs::Marker::ARROW;
        arrow.action = visualization_msgs::Marker::ADD;
        arrow.pose.orientation.w = 1;
        arrow.scale.x = 0.05;   // radius of cylinder
        arrow.scale.y = 0.1;
        arrow.scale.z = 0.03;   // scale of hat
        arrow.lifetime = ros::Duration();
        geometry_msgs::Point startPoint, endPoint;
        startPoint.x = 0;
        startPoint.y = 0;
        startPoint.z = 0;
        endPoint.x = 0;
        endPoint.y = 0;
        endPoint.z = 0;
        arrow.points.push_back(startPoint);
        arrow.points.push_back(endPoint);
        arrow.color.a = 1.0;

        std::string motorNames[5] = {"uav/motor0",
                                     "uav/motor1",
                                     "uav/motor2",
                                     "uav/motor3",
                                     "uav/motor4"};

        // publish moments
        auto Maero = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getMaero();
        fillArrow(arrow, Maero, Eigen::Vector3d(0.5, 0.5, 0.0));
        aeroMomentPub_.publish(arrow);

        auto Mmotors = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getMmotors();
        for(size_t motorIdx = 0; motorIdx < 5; motorIdx++){
            arrow.header.frame_id = motorNames[motorIdx].c_str();
            fillArrow(arrow, Mmotors[motorIdx], Eigen::Vector3d(0.5, 0.5, 0.0));
            motorsMomentsPub_[motorIdx].publish(arrow);
        }
        arrow.header.frame_id = UAV_FRAME_ID;

        auto Mtotal = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getMtotal();
        fillArrow(arrow, Mtotal, Eigen::Vector3d(0.0, 0.5, 0.5));
        totalMomentPub_.publish(arrow);


        // publish forces
        auto Faero = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getFaero();
        fillArrow(arrow, Faero / 10, Eigen::Vector3d(0.0, 0.5, 0.5));
        aeroForcePub_.publish(arrow);

        auto Fmotors = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getFmotors();
        for(size_t motorIdx = 0; motorIdx < 4; motorIdx++){
            fillArrow(arrow, Fmotors[motorIdx] / 10, Eigen::Vector3d(0.0, 0.5, 0.5));
            arrow.header.frame_id = motorNames[motorIdx].c_str();
            motorsForcesPub_[motorIdx].publish(arrow);
        }
        arrow.header.frame_id = "ICE";
        fillArrow(arrow, Fmotors[4] / 10, Eigen::Vector3d(0.0, 0.5, 0.5));
        motorsForcesPub_[4].publish(arrow);

        arrow.header.frame_id = UAV_FRAME_ID;
        auto Ftotal = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getFtotal();
        fillArrow(arrow, Ftotal, Eigen::Vector3d(0.0, 1.0, 1.0));
        totalForcePub_.publish(arrow);

        arrow.header.frame_id = UAV_FRAME_ID;
        auto velocity = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getBodyLinearVelocity();
        fillArrow(arrow, velocity, Eigen::Vector3d(0.7, 0.5, 1.3));
        velocityPub_.publish(arrow);

        auto Flift = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getFlift();
        fillArrow(arrow, Flift / 10, Eigen::Vector3d(0.8, 0.2, 0.3));
        liftForcePub_.publish(arrow);

        auto Fdrug = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getFdrug();
        fillArrow(arrow, Fdrug / 10, Eigen::Vector3d(0.2, 0.8, 0.3));
        drugForcePub_.publish(arrow);

        auto Fside = static_cast<InnoVtolDynamicsSim*>(uavDynamicsSim_)->getFside();
        fillArrow(arrow, Fside / 10, Eigen::Vector3d(0.2, 0.3, 0.8));
        sideForcePub_.publish(arrow);
    }
}
