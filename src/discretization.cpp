#include "discretization.h"

namespace pifl {

float computeFiniteDifferenceLaplacian(const FiniteDifferenceStencil& stencil, const float gridSpacing) {
  const float neighbors[] = {stencil.northTempC, stencil.southTempC,
                             stencil.eastTempC, stencil.westTempC};
  float sum = 0.0f;
  for (unsigned i = 0; i < 4; ++i) {
    sum += (stencil.zeroFluxMask & (1u << i)) ? stencil.centerTempC : neighbors[i];
  }
  return (sum -
          4.0f * stencil.centerTempC) /
         (gridSpacing * gridSpacing);
}

float explicitEulerStep(const float currentTempC, const float alpha, const float laplacian,
                        const float sourceTerm, const float dtSeconds) {
  return currentTempC + dtSeconds * (alpha * laplacian + sourceTerm);
}

Phase1StepResult advancePhase1Discretization(const FiniteDifferenceStencil& stencil, const float alpha,
                                             const float sourceTerm, const float gridSpacing,
                                             const float dtSeconds) {
  const float laplacian = computeFiniteDifferenceLaplacian(stencil, gridSpacing);
  return {
      laplacian,
      sourceTerm,
      explicitEulerStep(stencil.centerTempC, alpha, laplacian, sourceTerm, dtSeconds),
  };
}

}
