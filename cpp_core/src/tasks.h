#pragma once

#include <mujoco/mujoco.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <stdexcept>
#include <string>

// Reset and observation mirror the gymnasium env each policy was trained on.
struct TaskSpec {
    const char* name;
    const char* model_path;
    const char* policy_path;
    int obs_dim;
    int act_dim;
    int policy_us;  // the env's dt, i.e. the rate the policy was trained at
    void (*reset)(const mjModel*, mjData*, std::mt19937&);
    void (*observe)(const mjModel*, const mjData*, float* obs);
    void (*report)(const mjModel*, const mjData*);
};

namespace tasks {

inline int body_id(const mjModel* m, const char* name) {
    int id = mj_name2id(m, mjOBJ_BODY, name);
    if (id < 0) throw std::runtime_error(std::string("no body named ") + name);
    return id;
}

// InvertedPendulum-v5

inline void cartpole_reset(const mjModel* m, mjData* d, std::mt19937& rng) {
    mj_resetData(m, d);
    std::uniform_real_distribution<double> noise(-0.01, 0.01);
    for (int i = 0; i < m->nq; i++) d->qpos[i] += noise(rng);
    for (int i = 0; i < m->nv; i++) d->qvel[i] += noise(rng);
    mj_forward(m, d);
}

inline void cartpole_observe(const mjModel*, const mjData* d, float* obs) {
    obs[0] = (float)d->qpos[0];
    obs[1] = (float)d->qpos[1];
    obs[2] = (float)d->qvel[0];
    obs[3] = (float)d->qvel[1];
}

inline void cartpole_report(const mjModel*, const mjData* d) {
    std::printf("final pole angle  : %.5f rad\n", d->qpos[1]);
}

// Pusher-v5. qpos is 7 arm joints, then object y/x, then goal y/x.

inline void pusher_reset(const mjModel* m, mjData* d, std::mt19937& rng) {
    mj_resetData(m, d);

    std::uniform_real_distribution<double> obj_y(-0.3, 0.0);
    std::uniform_real_distribution<double> obj_x(-0.2, 0.2);
    double cy, cx;
    do {
        cy = obj_y(rng);
        cx = obj_x(rng);
    } while (std::hypot(cy, cx) <= 0.17);  // goal is at the origin

    d->qpos[7] = cy;
    d->qpos[8] = cx;
    d->qpos[9] = 0.0;
    d->qpos[10] = 0.0;

    std::uniform_real_distribution<double> vel(-0.005, 0.005);
    for (int i = 0; i < m->nv; i++) d->qvel[i] = vel(rng);
    for (int i = m->nv - 4; i < m->nv; i++) d->qvel[i] = 0.0;

    mj_forward(m, d);
}

inline void pusher_observe(const mjModel* m, const mjData* d, float* obs) {
    static const int tips = body_id(m, "tips_arm");
    static const int object = body_id(m, "object");
    static const int goal = body_id(m, "goal");

    for (int i = 0; i < 7; i++) obs[i] = (float)d->qpos[i];
    for (int i = 0; i < 7; i++) obs[7 + i] = (float)d->qvel[i];
    for (int k = 0; k < 3; k++) {
        obs[14 + k] = (float)d->xpos[3 * tips + k];
        obs[17 + k] = (float)d->xpos[3 * object + k];
        obs[20 + k] = (float)d->xpos[3 * goal + k];
    }
}

inline void pusher_report(const mjModel* m, const mjData* d) {
    int object = body_id(m, "object");
    int goal = body_id(m, "goal");
    double dx = d->xpos[3 * object] - d->xpos[3 * goal];
    double dy = d->xpos[3 * object + 1] - d->xpos[3 * goal + 1];
    std::printf("object to goal    : %.3f m\n", std::hypot(dx, dy));
}

inline const TaskSpec kAll[] = {
    {"cartpole", INVERTED_PENDULUM_PATH, POLICY_PATH, 4, 1, 40000,
     cartpole_reset, cartpole_observe, cartpole_report},
    {"pusher", PUSHER_MODEL_PATH, PUSHER_POLICY_PATH, 23, 7, 50000,
     pusher_reset, pusher_observe, pusher_report},
};

inline const TaskSpec& find(const std::string& name) {
    for (const auto& t : kAll) {
        if (name == t.name) return t;
    }
    throw std::runtime_error("unknown task " + name + ", expected cartpole or pusher");
}

}  // namespace tasks
