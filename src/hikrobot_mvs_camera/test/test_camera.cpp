#include "hikrobot_mvs_camera/camera_node.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

using namespace std::chrono_literals;
using hikrobot_mvs_camera::CameraBackend;
using hikrobot_mvs_camera::CameraNode;
using hikrobot_mvs_camera::Device;
using hikrobot_mvs_camera::Feature;
using hikrobot_mvs_camera::Frame;
using hikrobot_mvs_camera::GrabResult;
using hikrobot_mvs_camera::Selector;

namespace {

struct FakeState {
  std::atomic<bool> available{true};
  std::atomic<int> connections{0};
  std::atomic<int> exposure_writes{0};
  std::atomic<double> exposure{0};
  std::atomic<int> sequence{0};
  std::atomic<int> format_writes{0};
};

class FakeCamera final : public CameraBackend {
public:
  explicit FakeCamera(std::shared_ptr<FakeState> state) : state_(std::move(state)) {}

  bool connect(const Selector&, std::string& error) override {
    if (!state_->available) { error = "fake camera absent"; return false; }
    connected_ = true;
    ++state_->connections;
    return true;
  }
  void disconnect() override { connected_ = false; }
  bool connected() const override { return connected_; }
  std::string device_label() const override { return "fake [1]"; }
  GrabResult grab(Frame& frame, std::string& error) override {
    std::this_thread::sleep_for(5ms);
    if (!state_->available) { error = "fake disconnect"; return GrabResult::Disconnected; }
    frame.width = 2;
    frame.height = 2;
    frame.step = 2;
    frame.encoding = "mono8";
    frame.sequence = ++state_->sequence;
    frame.data = {1, 2, 3, 4};
    return GrabResult::Frame;
  }
  bool set_float(Feature feature, double value, std::string& error) override {
    if (value < 0 || value > 1000) { error = "fake range exceeded"; return false; }
    if (feature == Feature::Exposure) {
      state_->exposure = value;
      ++state_->exposure_writes;
    }
    return true;
  }
  bool set_pixel_format(const std::string& value, std::string& error) override {
    if (value != "Mono8") { error = "unsupported fake pixel format"; return false; }
    ++state_->format_writes;
    return true;
  }
private:
  std::shared_ptr<FakeState> state_;
  bool connected_ = false;
};

bool wait_for(const std::function<bool()>& condition, std::chrono::milliseconds limit = 2500ms) {
  const auto deadline = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < deadline) {
    if (condition()) return true;
    std::this_thread::sleep_for(10ms);
  }
  return condition();
}

class RosTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() { rclcpp::init(0, nullptr); }
  static void TearDownTestSuite() { rclcpp::shutdown(); }
};

TEST(Selection, RequiresExactlyOneMatch) {
  const std::vector<Device> devices = {
    {"U1", "MV-CA016-10UC"},
    {"U2", "MV-CA016-10UC"},
    {"X1", "OTHER"},
  };
  std::size_t index = 99;
  std::string error;
  EXPECT_FALSE(hikrobot_mvs_camera::select_device(devices, {}, index, error));
  EXPECT_NE(error.find("multiple"), std::string::npos);
  EXPECT_TRUE(hikrobot_mvs_camera::select_device(devices, {"U1"}, index, error));
  EXPECT_EQ(index, 0u);
  EXPECT_FALSE(hikrobot_mvs_camera::select_device(devices, {"MISSING"}, index, error));
}

TEST_F(RosTest, PublishesFrameAndRestoresAcceptedSettingAfterReconnect) {
  auto state = std::make_shared<FakeState>();
  rclcpp::NodeOptions options;
  options.parameter_overrides({rclcpp::Parameter("exposure_time_us", 200.0)});
  auto camera = std::make_shared<CameraNode>(std::make_unique<FakeCamera>(state), options);
  auto listener = std::make_shared<rclcpp::Node>("fake_listener");
  std::atomic<int> received{0};
  auto subscription = listener->create_subscription<sensor_msgs::msg::Image>(
    "/image_raw", rclcpp::SensorDataQoS(),
    [&received](const sensor_msgs::msg::Image& image) {
      if (image.width == 2 && image.height == 2 && image.step == 2 &&
          image.encoding == "mono8" && image.data == std::vector<uint8_t>({1, 2, 3, 4})) {
        ++received;
      }
    });
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(listener);
  ASSERT_TRUE(wait_for([&] { executor.spin_some(); return received > 0; }));
  EXPECT_EQ(state->exposure.load(), 200.0);
  auto result = camera->set_parameter(rclcpp::Parameter("exposure_time_us", 300.0));
  ASSERT_TRUE(result.successful) << result.reason;
  EXPECT_EQ(state->exposure.load(), 300.0);
  result = camera->set_parameter(rclcpp::Parameter("gain_db", 0.0));
  EXPECT_TRUE(result.successful) << result.reason;
  result = camera->set_parameter(rclcpp::Parameter("exposure_time_us", 2000.0));
  EXPECT_FALSE(result.successful);
  EXPECT_EQ(state->exposure.load(), 300.0);
  result = camera->set_parameter(rclcpp::Parameter("pixel_format", "Unsupported"));
  EXPECT_FALSE(result.successful);
  result = camera->set_parameter(rclcpp::Parameter("pixel_format", "Mono8"));
  ASSERT_TRUE(result.successful) << result.reason;
  state->available = false;
  std::this_thread::sleep_for(100ms);
  state->available = true;
  ASSERT_TRUE(wait_for([&] {
    return state->connections >= 2 && state->exposure_writes >= 3 &&
           state->format_writes >= 2;
  }));
  EXPECT_EQ(state->exposure.load(), 300.0);
  (void)subscription;
}

}  // namespace
