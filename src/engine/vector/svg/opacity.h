#pragma once
// Effective alpha. Parent times local.
namespace pittore::svg {

float effectiveOpacity(float parent, float local, float extra = 1.0f);

}  // namespace pittore::svg
