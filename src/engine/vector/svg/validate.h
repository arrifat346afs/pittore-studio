#pragma once
// Scene checks. Missing refs and empty geom.
#include <string>
#include <vector>

namespace pittore::svg {

struct Scene;

struct Issue {
    std::string id;
    std::string msg;
};

std::vector<Issue> validateScene(const Scene& scene);

}  // namespace pittore::svg
