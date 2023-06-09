#include <virtual_navigation_layers/virtual_layer.h>
#include <ros/ros.h>
#include <pluginlib/class_list_macros.h>
#include <list>
#include <boost/algorithm/string.hpp>
#include <geometry_msgs/PointStamped.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>
#include <map>
#include <string>


PLUGINLIB_EXPORT_CLASS(virtual_navigation_layers::VirtualLayer, costmap_2d::Layer)

using costmap_2d::NO_INFORMATION;
using costmap_2d::LETHAL_OBSTACLE;
using costmap_2d::FREE_SPACE;

namespace virtual_navigation_layers{

VirtualLayer::VirtualLayer() {}

bool checkPointInList(const geometry_msgs::PointStamped point, std::list<geometry_msgs::PointStamped>& pointList){
    unsigned int size = pointList.size();
    ROS_INFO("Pointlist size: %d", size);
    bool remove = false;
    std::list<geometry_msgs::PointStamped> temp;

    std::list<geometry_msgs::PointStamped>::iterator p_it;
    // check if there are items to remove
    if (size < 1){
        ROS_INFO("Got new point");
        return false;
    }
    else{
        double c_point_x = point.point.x, c_point_y = point.point.y;
        for (p_it = pointList.begin(); p_it != pointList.end(); ++p_it){
            geometry_msgs::PointStamped l_point = *p_it;
            double dist = sqrt(pow(c_point_x - l_point.point.x,2) + pow(c_point_y - l_point.point.y,2));
            ROS_INFO("%f", dist);
            if ((abs(dist)) < 0.1){
                remove = true;
                temp.push_back(*p_it);
            }
        }
    }

    // remove existing items
    if (remove){
        ROS_INFO("Found existing point... removing");
        std::list<geometry_msgs::PointStamped>::iterator it;
        for (it = temp.begin(); it != temp.end(); ++it)
            pointList.remove(*it);
        return true;
    }
    else{
        ROS_INFO("Got new point");
        return false;
    }
}

void VirtualLayer::onInitialize(){
    ros::NodeHandle nh("~/" + name_), g_nh;
    ROS_INFO("Initializing VirtualLayer Plugin...");

    transformedPoints_.clear();

    cb_ = boost::bind(&VirtualLayer::reconfigure, this, _1, _2);
    dsrv_ = new dynamic_reconfigure::Server<VirtualLayerConfig>(ros::NodeHandle("~/" + name_));
    dsrv_->setCallback(cb_);

    sub_point_ = nh.subscribe("/clicked_point", 1, &VirtualLayer::cbPoint, this);
    g_sub_point_ = g_nh.subscribe("/clicked_point", 1, &VirtualLayer::cbGPoint, this);

    pub_clicked_point_marker_ = nh.advertise<visualization_msgs::MarkerArray>("/cost_point", 1);
    

    visualization_msgs::Marker marker;
    marker.id = 0;
    marker.action = 3;
    
    removeMarkers_.markers.push_back(marker);

    // inflation_layer_ = new costmap_2d::INflationLayer();

    first_time_ = true;
    current_ = true;
    cost_ = static_cast<unsigned char>(254);
    
}

void VirtualLayer::updateBounds(double robot_x, double robot_y, double robot_yaw,
                            double* min_x, double* min_y, double* max_x, double* max_y){
    
    boost::recursive_mutex::scoped_lock lock(lock_);
    std::string global_frame = layered_costmap_->getGlobalFrameID();

    // inflation_layer_->updateBounds(robot_x, robot_y, robot_yaw, min_x, min_y, max_x, max_y);

    if (first_time_){
        last_min_x_ = *min_x;
        last_min_y_ = *min_y;
        last_max_x_ = *max_x;
        last_max_y_ = *max_y;
        first_time_ = false;
    }
    else{
        double a = *min_x, b = *min_y, c = *max_x, d = *max_y;
        *min_x = std::min(last_min_x_, *min_x);
        *min_y = std::min(last_min_y_, *min_y);
        *max_x = std::max(last_max_x_, *max_x);
        *max_y = std::max(last_max_y_, *max_y);
        last_min_x_ = a;
        last_min_y_ = b;
        last_max_x_ = c;
        last_max_y_ = d;
    }

    
}

void VirtualLayer::updateCosts(costmap_2d::Costmap2D& master_grid, int min_i, int min_j, int max_i, int max_j){
    
    boost::recursive_mutex::scoped_lock lock(lock_);
    pub_clicked_point_marker_.publish(removeMarkers_);
    if (transformedPoints_.empty()) return;
    // Get costmap
    costmap_2d::Costmap2D* costmap = layered_costmap_->getCostmap();

    double res = costmap->getResolution();
    // ROS_INFO("Updating costs...");
    visualization_msgs::MarkerArray markerArray;
    int id = 0;

    std::list<geometry_msgs::PointStamped>::iterator p_it;
    for (p_it = transformedPoints_.begin(); p_it != transformedPoints_.end(); ++p_it){
        geometry_msgs::PointStamped point = *p_it;
        double cx = point.point.x, cy = point.point.y;

        visualization_msgs::Marker marker;
        marker.header = point.header;
        marker.id = id;
        marker.action = 0;
        marker.type = 3;
        marker.pose.position.x = point.point.x;
        marker.pose.position.y = point.point.y;
        marker.pose.position.z = point.point.z;
        marker.pose.orientation.x = 0;
        marker.pose.orientation.y = 0;
        marker.pose.orientation.z = 0;
        marker.pose.orientation.w = 1;
        marker.scale.x = 0.1;
        marker.scale.y = 0.1;
        marker.scale.z = 0.05;
        marker.color.b = 1.0;
        marker.color.a = 0.7;
        marker.lifetime = ros::Duration(0);
        id++;
        markerArray.markers.push_back(marker);


        int radius = static_cast<int>(size_/2/res);

        int obs_x, obs_y;
        costmap->worldToMapNoBounds(cx, cy, obs_x, obs_y);

        // for (int i = min_i; i < max_i; i++)
        //     for (int j = min_j; j < max_j; j++)
        //         if (((obs_x - i)*(obs_x - i) + (obs_y - j)*(obs_y - j)) < radius*radius) 
        //             costmap->setCost(i, j, cost_);

        unsigned char* master_array = master_grid.getCharMap();
        unsigned int span = master_grid.getSizeInCellsX();

        for (int j = min_j; j < max_j; j++){
            unsigned int it = j * span + min_i;
            for (int i = min_i; i < max_i; i++){
                if (((obs_x - i)*(obs_x - i) + (obs_y - j)*(obs_y - j)) < radius*radius) 
                    master_array[it] = cost_;
                it++;
            }
        }
        // costmap_2d::Costmap2D inflated_costmap;
        // inflation_layer_->getCostmap();
    
    }

    pub_clicked_point_marker_.publish(markerArray);
}

void VirtualLayer::cbPoint(const geometry_msgs::PointStamped& point){

    boost::recursive_mutex::scoped_lock lock(lock_);
    ROS_INFO("Checking new point");

    std::string global_frame = layered_costmap_->getGlobalFrameID();
    geometry_msgs::PointStamped in, out;
    in.header.frame_id = point.header.frame_id;
    in.header.stamp = point.header.stamp;
    in.point = point.point;

    tf_->transform(in, out, global_frame);

    // remove point if new point is already existing
    if (checkPointInList(out, transformedPoints_))
        return;
    else
        transformedPoints_.push_back(out);

}

void VirtualLayer::cbGPoint(const geometry_msgs::PointStamped& point){
    return;
}

void VirtualLayer::reconfigure(VirtualLayerConfig& config, uint32_t level){
    enabled_ = config.enabled;
    size_ = config.size;
    cost_ = static_cast<unsigned char>(config.cost);
}

}