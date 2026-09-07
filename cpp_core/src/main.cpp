#include <iostream>
#include <mujoco/mujoco.h>

class Model {
    mjModel* m;
public:
    Model(const char* filename) {
        char error[1024] = {};
        m = mj_loadXML(filename, nullptr, error, 1024);
        if (!m) throw std::runtime_error(std::string("Failed to load model: ") + error);
        std::cout << "Loaded model with " << m->nq << " degrees of freedom\n";
    }

    ~Model() {
        if (m) mj_deleteModel(m);
    }

    mjModel* get() const { return m; }
    int nq() const { return m->nq; }
};

class Data {
    mjData* d;
public:
    Data(const Model& model) {
        d = mj_makeData(model.get());
        if (!d) throw std::runtime_error("Failed to create data");
    }

    ~Data() {
        if (d) mj_deleteData(d);
    }

    mjData* get() const { return d; }
};

int main() {
    const char* model_path = "/Users/calvinzhou/ai-workspace/ferro/training/.venv/lib/python3.12/site-packages/mujoco/testdata/model.xml";

    {
        Model model(model_path);
        Data data(model);

        std::cout << "Stepping simulation 10 times...\n";
        for (int i = 0; i < 10; i++) {
            mj_step(model.get(), data.get());
            std::cout << "Step " << i << ": qpos[0] = " << data.get()->qpos[0] << "\n";
        }

        std::cout << "Done!\n";
    }

    return 0;
}
