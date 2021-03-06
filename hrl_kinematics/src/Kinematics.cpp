// SVN $HeadURL$
// SVN $Id$

/*
 * hrl_kinematics - a kinematics library for humanoid robots based on KDL
 *
 * Copyright 2011-2012 Armin Hornung, University of Freiburg
 * License: BSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University of Freiburg nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <hrl_kinematics/Kinematics.h>

using robot_state_publisher::SegmentPair;

namespace hrl_kinematics {

Kinematics::Kinematics()
: nh_(), nh_private_ ("~"),
  root_link_name_("base_link"), rfoot_link_name_("r_sole"),  lfoot_link_name_("l_sole")
{
  // Get URDF XML
  std::string urdf_xml, full_urdf_xml;
  nh_private_.param("robot_description_name",urdf_xml,std::string("robot_description"));
  nh_.searchParam(urdf_xml,full_urdf_xml);

  ROS_DEBUG("Reading xml file from parameter server");
  std::string result;


  if (!nh_.getParam(full_urdf_xml, result))
    throw Kinematics::InitFailed("Could not load the xml from parameter server: " + urdf_xml);


  // Load and Read Models
  if (!loadModel(result))
    throw Kinematics::InitFailed("Could not load models!");

  ROS_INFO("Kinematics initialized");

}

Kinematics::~Kinematics() {

}

bool Kinematics::loadModel(const std::string xml) {

  if (!urdf_model_.initString(xml)) {
    ROS_FATAL("Could not initialize robot model");
    return -1;
  }


  if (!kdl_parser::treeFromUrdfModel(urdf_model_, kdl_tree_)) {
    ROS_ERROR("Could not initialize tree object");
    return false;
  }


  // walk the tree and add segments to segments_
  addChildren(kdl_tree_.getRootSegment());


  if (!(kdl_tree_.getChain(root_link_name_, rfoot_link_name_, kdl_chain_right_)
      && kdl_tree_.getChain(root_link_name_, lfoot_link_name_, kdl_chain_left_))) {
    ROS_ERROR("Could not initialize leg chain objects");
    return false;
  }

  return true;
}



// add children to correct maps
void Kinematics::addChildren(const KDL::SegmentMap::const_iterator segment)
{
  const std::string& root = segment->second.segment.getName();

  const std::vector<KDL::SegmentMap::const_iterator>& children = segment->second.children;
  for (unsigned int i=0; i<children.size(); i++){
    const KDL::Segment& child = children[i]->second.segment;
    SegmentPair s(children[i]->second.segment, root, child.getName());
    if (child.getJoint().getType() == KDL::Joint::None){
      // skip over fixed:
      //      segments_fixed_.insert(make_pair(child.getJoint().getName(), s));
      ROS_DEBUG("Tree initialization: Skipping fixed segment from %s to %s", root.c_str(), child.getName().c_str());
    }
    else{
      segments_.insert(make_pair(child.getJoint().getName(), s));
      ROS_DEBUG("Tree initialization: Adding moving segment from %s to %s", root.c_str(), child.getName().c_str());
    }
    addChildren(children[i]);
  }
}


void Kinematics::createCoGMarker(const std::string& ns, const std::string& frame_id, double radius, const KDL::Vector& cog, visualization_msgs::Marker& marker) const{
  marker.header.frame_id = frame_id;
  marker.ns = ns;
  marker.type = visualization_msgs::Marker::SPHERE;
  marker.action = visualization_msgs::Marker::ADD;
  marker.pose.position.x = cog.x();
  marker.pose.position.y = cog.y();
  marker.pose.position.z = cog.z();
  marker.scale.x = radius;
  marker.scale.y = radius;
  marker.scale.z = radius;
  marker.color.r = 1.0;
  marker.color.a = 0.8;
}

void Kinematics::computeCOMRecurs(const KDL::SegmentMap::const_iterator& current_seg, const std::map<std::string, double>& joint_positions,
                                  const KDL::Frame& tf, KDL::Frame& tf_right_foot, KDL::Frame& tf_left_foot, double& m, KDL::Vector& com) {

  double jnt_p = 0.0;

  if (current_seg->second.segment.getJoint().getType() != KDL::Joint::None){
    std::map<std::string, double>::const_iterator jnt = joint_positions.find(current_seg->second.segment.getJoint().getName());

    if (jnt == joint_positions.end()){
      ROS_WARN("Could not find joint %s of %s in joint positions. Aborting tree branch.", current_seg->second.segment.getJoint().getName().c_str(), current_seg->first.c_str());
      return;
    }
    jnt_p = jnt->second;
  }

  KDL::Frame current_frame = tf * current_seg->second.segment.pose(jnt_p);
  if (current_seg->first == lfoot_link_name_){
    tf_left_foot = current_frame;
    ROS_DEBUG("Right foot tip transform found");
  } else if (current_seg->first == rfoot_link_name_){
    tf_right_foot = current_frame;
    ROS_DEBUG("Left foot tip transform found");
  }


  KDL::Vector current_cog = current_seg->second.segment.getInertia().getCOG();
  double current_m = current_seg->second.segment.getInertia().getMass();


  com = com + current_m * (current_frame*current_cog);

  m += current_m;
  ROS_DEBUG("At link %s. local: %f / [%f %f %f]; global: %f / [%f %f %f]",current_seg->first.c_str(), current_m, current_cog.x(), current_cog.y(), current_cog.z(),
            m, com.x(), com.y(), com.z());

  // TODO: separate recursive fct to create markers, callable on demand
//  if (current_m > 0.0){
//    visualization_msgs::Marker marker;
//    createCoGMarker(current_seg->first, "torso", 0.02, (current_frame*current_cog), marker);
//    com_vis_markers_.markers.push_back(marker);
//  }

  std::vector<KDL::SegmentMap::const_iterator >::const_iterator child_it;
  for (child_it = current_seg->second.children.begin(); child_it !=current_seg->second.children.end(); ++child_it){
    computeCOMRecurs(*child_it, joint_positions, current_frame, tf_right_foot, tf_left_foot, m, com);
  }

}

void Kinematics::computeCOM(const std::map<std::string, double>& joint_positions, tf::Point& COM, double& mass,
                            tf::Transform& tf_right_foot, tf::Transform& tf_left_foot){
  mass = 0.0;
  KDL::Vector com;
  KDL::Frame ident = KDL::Frame::Identity();
  KDL::Frame transform = ident;
  KDL::Frame right_foot_tf = ident;
  KDL::Frame left_foot_tf = ident;

  computeCOMRecurs(kdl_tree_.getRootSegment(), joint_positions, transform, right_foot_tf, left_foot_tf, mass, com);
  if (left_foot_tf == ident || right_foot_tf == ident){
    ROS_ERROR("Could not obtain left or right foot transforms");
    return;
  }

  if (mass <= 0.0){
    ROS_WARN("Total mass is 0, no CoM possible.");
    COM.setValue(0,0,0);
    return;
  }

  com = 1.0/mass * com;
  ROS_DEBUG("Total mass: %f CoG: (%f %f %f)", mass, com.x(), com.y(), com.z());

  COM.setValue(com.x(), com.y(), com.z());
  tf::TransformKDLToTF(right_foot_tf, tf_right_foot);
  tf::TransformKDLToTF(left_foot_tf, tf_left_foot);
}



} /* namespace hrl_kinematics */
