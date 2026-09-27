// Prints the C++ observation for states read from stdin, one per line:
// qpos values then qvel values. Compared against gymnasium by
// training/check_obs_parity.py.

#include "mujoco_wrapper.h"
#include "tasks.h"

#include <cstdio>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <task>\n", argv[0]);
        return 1;
    }
    const TaskSpec& task = tasks::find(argv[1]);
    Model model(task.model_path);
    Data data(model);
    const mjModel* m = model.get();
    mjData* d = data.get();

    float obs[64];
    while (true) {
        for (int i = 0; i < m->nq; i++) if (!(std::cin >> d->qpos[i])) return 0;
        for (int i = 0; i < m->nv; i++) if (!(std::cin >> d->qvel[i])) return 0;
        mj_forward(m, d);
        task.observe(m, d, obs);
        for (int i = 0; i < task.obs_dim; i++) std::printf(i ? " %.9g" : "%.9g", obs[i]);
        std::printf("\n");
    }
}
