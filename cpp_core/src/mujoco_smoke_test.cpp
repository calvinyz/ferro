#include <cstdio>
#include <mujoco/mujoco.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <model.xml>\n", argv[0]);
        return 1;
    }

    char error[1000] = {0};
    mjModel* model = mj_loadXML(argv[1], nullptr, error, sizeof(error));
    if (!model) {
        std::fprintf(stderr, "mj_loadXML failed: %s\n", error);
        return 1;
    }

    mjData* data = mj_makeData(model);

    for (int step = 0; step < 500; ++step) {
        mj_step(model, data);
        if (step % 100 == 0) {
            std::printf("step %4d  t=%.3f  qpos[0]=%.5f\n", step, data->time, data->qpos[0]);
        }
    }

    mj_deleteData(data);
    mj_deleteModel(model);
    return 0;
}
