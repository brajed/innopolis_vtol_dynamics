#!/bin/bash

cd "$(dirname "$0")"
source docker/config.sh
source /opt/ros/$ROS_DISTRO/setup.bash
source /catkin_ws/devel/setup.bash

# if DEV_NAME env variable is not initialized, this script will try to define it on his own
echo "DEV_NAME is" $DEV_NAME
./uavcan_tools/create_slcan_from_serial.sh $DEV_NAME
roslaunch innopolis_vtol_dynamics hitl.launch   \
    vehicle:=iris                               \
    airframe:=iris                              \
    dynamics:=flightgoggles_multicopter