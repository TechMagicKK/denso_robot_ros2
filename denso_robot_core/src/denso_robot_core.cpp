/**
 * Software License Agreement (MIT License)
 *
 * @copyright Copyright (c) 2015 DENSO WAVE INCORPORATED
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "denso_robot_core/denso_robot_core.h"
#include "denso_robot_core/denso_controller_rc8.h"
#include "denso_robot_core/denso_controller_rc8_cobotta.h"
#include "denso_robot_core/denso_controller_rc9.h"

int main(int argc, char** argv)
{
  rclcpp::init(argc, argv);
  HRESULT hr;

  auto node = rclcpp::Node::make_shared("denso_robot_core");

  node->declare_parameter("denso_ip_address", "127.0.0.1");
  node->declare_parameter("denso_controller_type", 0);
  node->declare_parameter("denso_robot_model", "");

  std::string ip_address;
  std::string robot_name = "";
  std::string robot_model = "";
  std::string cobotta_model = "cobotta";
  int ctrl_type = 8;
  node->get_parameter("denso_ip_address", ip_address);
  node->get_parameter("denso_controller_type", ctrl_type);
  node->get_parameter("denso_robot_model", robot_model);

  if (!strncmp(robot_model.c_str(), cobotta_model.c_str(), robot_model.length())) {
    robot_name = "CVR038A1";
  }

  denso_robot_core::DensoRobotCore engine(node, ip_address, robot_name, ctrl_type);

  hr = engine.Initialize();
  if (FAILED(hr)) {
    RCLCPP_FATAL(rclcpp::get_logger(node->get_name()), "Failed to initialize. (%X)", hr);
    return 1;
  } else {
    std::thread t(std::bind(&denso_robot_core::DensoRobotCore::Start, &engine));

    engine.Stop();
    t.join();
    return 0;
  }

}

namespace denso_robot_core
{
DensoRobotCore::DensoRobotCore(
  rclcpp::Node::SharedPtr& node, const std::string& ip_address,
  const std::string& robot_name, int ctrl_type)
: m_node(node), m_ctrlType(ctrl_type), m_mode(0), m_quit(false),
  m_addr(ip_address), m_robName(robot_name)
{
  m_ctrl.reset();
}

DensoRobotCore::~DensoRobotCore()
{
}

HRESULT DensoRobotCore::Initialize()
{

  if (m_node == NULL) {
    m_node = rclcpp::Node::make_shared("denso_robot_core");
  }

  std::string name, filename;
  float ctrl_cycle_msec;

  m_node->declare_parameter("denso_controller_name", "");
  m_node->declare_parameter("denso_config_file", "");
  m_node->declare_parameter("denso_bcap_slave_control_cycle_msec", 8.0);

  if(!m_node->get_parameter("denso_controller_name", name)) {
    name = "";
  }
  if(!m_node->get_parameter("denso_config_file", filename)) {
    return E_FAIL;
  }
  if(!m_node->get_parameter("denso_bcap_slave_control_cycle_msec", ctrl_cycle_msec)) {
    return E_FAIL;
  }

  // #################################################

  switch (m_ctrlType) {
    case 8:
      if (DensoControllerRC8Cobotta::IsCobotta(m_robName)) {
        m_ctrl = std::make_shared<DensoControllerRC8Cobotta>(
          m_node, name, &m_mode, m_addr,
          rclcpp::Duration(std::chrono::duration<double>(ctrl_cycle_msec / 1000.0)));
        RCLCPP_INFO(
          rclcpp::get_logger(m_node->get_name()), "****** controller hardware: RC8 cobotta");
      } else {
        m_ctrl = std::make_shared<DensoControllerRC8>(
          m_node, name, &m_mode, m_addr,
          rclcpp::Duration(std::chrono::duration<double>(ctrl_cycle_msec / 1000.0)));
        RCLCPP_INFO(rclcpp::get_logger(m_node->get_name()), "****** controller hardware: RC8");
      }
      break;
    case 9:
      m_ctrl = std::make_shared<DensoControllerRC9>(
        m_node, name, &m_mode, m_addr, rclcpp::Duration(std::chrono::duration<double>(ctrl_cycle_msec / 1000.0)));
      RCLCPP_INFO(rclcpp::get_logger(m_node->get_name()), "****** controller hardware: RC9");
      break;
    default:
      RCLCPP_FATAL(
        rclcpp::get_logger(m_node->get_name()), "Invalid argument value [controller_type]");
      return E_INVALIDARG;
  }
  RCLCPP_INFO(
    rclcpp::get_logger(m_node->get_name()), "****** DENSO robot control cycle [ms] : %.1f ", ctrl_cycle_msec);
  // RCLCPP_INFO(rclcpp::get_logger(m_node->get_name()), "****** [DEBUG] Initializing bcap connection... ");
  return m_ctrl->InitializeBCAP(filename);
}

void DensoRobotCore::Start()
{
  m_quit = false;
  m_ctrl->StartService(m_node);

  rclcpp::WallRate loop_rate(1000);
  while (!m_quit && rclcpp::ok()) {
    rclcpp::spin_some(m_node);
    m_ctrl->Update();
    loop_rate.sleep();
  }
}

void DensoRobotCore::Stop()
{
  m_quit = true;
  m_ctrl->StopService();
}

HRESULT DensoRobotCore::ChangeMode(int mode, bool service)
{
  m_ctrl->StopService();

  DensoRobot_Ptr pRob;
  HRESULT hr = m_ctrl->get_Robot(0, &pRob);
  if (SUCCEEDED(hr)) {
    switch (m_ctrlType) {
      case 8:
      case 9:
        hr = pRob->ChangeMode(mode);
        break;
      default:
        hr = E_FAIL;
        break;
    }
  }

  m_mode = SUCCEEDED(hr) ? mode : 0;
  if ((m_mode == 0) && service) {
    m_ctrl->StartService(m_node);
  }

  return hr;
}


}  // namespace denso_robot_core
