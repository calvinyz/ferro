#pragma once

#ifdef FERRO_WITH_ROS

#include "target_channel.h"

#include <mujoco/mujoco.h>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <std_msgs/msg/float32_multi_array.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

// Publishes /joint_states and /ferro/command from the latest-value channels.
// Seqlock readers never block the writer, so this reads the control loop's
// state without touching its timing. ROS 2 publishing allocates and goes
// through DDS, which is why it lives on its own thread at normal priority.
class RosBridge {
    const TargetChannel& state_ch;
    const TargetChannel& cmd_ch;
    int nq, act_dim;
    std::vector<std::string> names;
    std::vector<int> qpos_adr, dof_adr;
    std::atomic<bool> running{true};
    std::thread worker;

public:
    RosBridge(const mjModel* m, const TargetChannel& state, const TargetChannel& cmd,
              int act_dim, int rate_hz)
        : state_ch(state), cmd_ch(cmd), nq(m->nq), act_dim(act_dim) {
        for (int j = 0; j < m->njnt; j++) {
            const char* n = mj_id2name(m, mjOBJ_JOINT, j);
            names.push_back(n ? n : "joint" + std::to_string(j));
            qpos_adr.push_back(m->jnt_qposadr[j]);
            dof_adr.push_back(m->jnt_dofadr[j]);
        }

        if (!rclcpp::ok()) rclcpp::init(0, nullptr);

        worker = std::thread([this, rate_hz] {
#if defined(__linux__)
            sched_param normal{};
            pthread_setschedparam(pthread_self(), SCHED_OTHER, &normal);
#endif
            auto node = rclcpp::Node::make_shared("ferro_bridge");
            auto js_pub = node->create_publisher<sensor_msgs::msg::JointState>("joint_states", 10);
            auto cmd_pub = node->create_publisher<std_msgs::msg::Float32MultiArray>("ferro/command", 10);

            sensor_msgs::msg::JointState js;
            js.name = names;
            js.position.resize(names.size());
            js.velocity.resize(names.size());
            std_msgs::msg::Float32MultiArray cmd_msg;
            cmd_msg.data.resize(this->act_dim);

            auto period = std::chrono::microseconds(1'000'000 / rate_hz);
            auto next = std::chrono::steady_clock::now();

            while (running.load(std::memory_order_relaxed) && rclcpp::ok()) {
                next += period;

                Target s{};
                if (state_ch.try_read(s) && s.policy_tick > 0) {
                    js.header.stamp = node->now();
                    for (size_t j = 0; j < names.size(); j++) {
                        js.position[j] = s.value[qpos_adr[j]];
                        js.velocity[j] = s.value[nq + dof_adr[j]];
                    }
                    js_pub->publish(js);
                }

                Target c{};
                if (cmd_ch.try_read(c) && c.policy_tick > 0) {
                    for (int i = 0; i < this->act_dim; i++) cmd_msg.data[i] = c.value[i];
                    cmd_pub->publish(cmd_msg);
                }

                std::this_thread::sleep_until(next);
            }
        });
    }

    ~RosBridge() {
        running.store(false, std::memory_order_relaxed);
        if (worker.joinable()) worker.join();
        rclcpp::shutdown();
    }

    RosBridge(const RosBridge&) = delete;
    RosBridge& operator=(const RosBridge&) = delete;
};

#endif
