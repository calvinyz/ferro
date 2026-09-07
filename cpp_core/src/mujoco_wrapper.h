#pragma once

#include <mujoco/mujoco.h>
#include <iostream>

class Model {
    mjModel* m;
public:
    Model(const char* filename) {
        m = mj_loadXML(filename, nullptr);
        if (!m) throw std::runtime_error("Failed to load model");
        std::cout << "Loaded model: " << filename << "\n";
    }
    
    ~Model() {
        mj_deleteModel(m);
    }
    
    mjModel* get() const { return m; }
};

class Data {
    mjData* d;
public:
    Data(const Model& model) {
        d = mj_makeData(model.get());
        if (!d) throw std::runtime_error("Failed to make data");
        std::cout << "Created data\n";
    }
    
    ~Data() {
        mj_deleteData(d);
    }
    
    mjData* get() const { return d; }
};
