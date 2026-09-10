#pragma once

#include <iostream>
#include <mujoco/mujoco.h>
#include <stdexcept>
#include <string>

class Model {
    mjModel* m;

public:
    explicit Model(const char* filename) {
        char error[1024] = {};
        m = mj_loadXML(filename, nullptr, error, sizeof(error));
        if (!m) throw std::runtime_error(std::string("Failed to load model: ") + error);
        std::cout << "Loaded model with " << m->nq << " degrees of freedom\n";
    }

    ~Model() {
        if (m) mj_deleteModel(m);
    }

    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    mjModel* get() const { return m; }
    int nq() const { return m->nq; }
};

class Data {
    mjData* d;

public:
    explicit Data(const Model& model) {
        d = mj_makeData(model.get());
        if (!d) throw std::runtime_error("Failed to create data");
    }

    ~Data() {
        if (d) mj_deleteData(d);
    }

    Data(const Data&) = delete;
    Data& operator=(const Data&) = delete;

    mjData* get() const { return d; }
};
