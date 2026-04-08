#pragma once

namespace pifl {

struct FiniteDifferenceStencil {
  float centerTempC;
  float northTempC;
  float southTempC;
  float eastTempC;
  float westTempC;
};

struct Phase1StepResult {
  float laplacian;
  float sourceTerm;
  float nextTempC;
};

float computeFiniteDifferenceLaplacian(const FiniteDifferenceStencil& stencil, float gridSpacing);
float explicitEulerStep(float currentTempC, float alpha, float laplacian, float sourceTerm, float dtSeconds);
Phase1StepResult advancePhase1Discretization(const FiniteDifferenceStencil& stencil, float alpha,
                                             float sourceTerm, float gridSpacing, float dtSeconds);

}
