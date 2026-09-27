// Reads states from stdin, one per line: qpos, qvel, ctrl. Applies ctrl for
// argv[2] physics steps and prints the resulting observation, which
// training/check_obs_parity.py compares against gymnasium. Stepping means a
// model file that differs from the training env shows up, not just a wrong
// observation formula.

#include "mujoco_wrapper.h"
#include "tasks.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <task> <steps>\n", argv[0]);
        return 1;
    }
    const TaskSpec& task = tasks::find(argv[1]);
    const int steps = std::atoi(argv[2]);

    Model model(task.model_path);
    Data data(model);
    const mjModel* m = model.get();
    mjData* d = data.get();

    float obs[64];
    while (true) {
        for (int i = 0; i < m->nq; i++) if (!(std::cin >> d->qpos[i])) return 0;
        for (int i = 0; i < m->nv; i++) if (!(std::cin >> d->qvel[i])) return 0;
        for (int i = 0; i < m->nu; i++) if (!(std::cin >> d->ctrl[i])) return 0;
        mj_forward(m, d);
        for (int s = 0; s < steps; s++) mj_step(m, d);
        task.observe(m, d, obs);
        for (int i = 0; i < task.obs_dim; i++) std::printf(i ? " %.9g" : "%.9g", obs[i]);
        std::printf("\n");
    }
}
