// Based on `examples/rclcpp/minimal_publisher` @ 2dbcf9f
#include <chrono>

#include "rclcpp/rclcpp.hpp"
#define STRINGIFY(x) #x
#define POODLE(PKG, MSG) STRINGIFY(PKG/msg/MSG.hpp)
#include POODLE(std_msgs, string)

namespace {

class Pub : public rclcpp::Node {
 public:
  Pub() : Node("pub") {
    using namespace std::chrono_literals;
    auto publisher = this->create_publisher<std_msgs::msg::String>("topic");
    auto callback = [this, publisher]() {
      std_msgs::msg::String message;
      message.data = "Hello, world! " + std::to_string(count_);
      count_ += 1;
      std::cout << "Pub: " << message.data << "\n";
      publisher->publish(message);
    };
    timer_ = this->create_wall_timer(500ms, callback);
  }

 private:
  rclcpp::TimerBase::SharedPtr timer_;
  int count_{};
};

}  // namespace

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  std::cout << rmw_get_implementation_identifier() << std::endl;
  rclcpp::spin(std::make_shared<Pub>());
  rclcpp::shutdown();
  return 0;
}
