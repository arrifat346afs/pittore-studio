#pragma once
// Expand use nodes by cloning targets. Capped depth.
namespace pittore::svg {

struct SceneNode;

int expandUses(SceneNode& root);

}  // namespace pittore::svg
