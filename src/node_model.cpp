#include "node_model.h"
#include <math.h>

namespace pifl {

namespace {

float clampAbsValue(const float value, const float bound) {
  if (value > bound) {
    return bound;
  }
  if (value < -bound) {
    return -bound;
  }
  return value;
}

}

float evaluateQuadraticCorrection(const ModelParameters& model, const float temperatureC) {
  return model.w1 * temperatureC + model.w2 * temperatureC * temperatureC + model.b;
}

Phase2Prediction predictPhase2NodeStep(const FiniteDifferenceStencil& stencil, const float alpha,
                                       const ModelParameters& model, const float gridSpacing,
                                       const float dtSeconds) {
  const float maxStableDt = 0.25f * gridSpacing * gridSpacing / alpha;
  const int numSteps = static_cast<int>(ceilf(dtSeconds / maxStableDt));
  const float subDt = dtSeconds / static_cast<float>(numSteps);

  FiniteDifferenceStencil currentStencil = stencil;
  float finalCorrectionTerm = 0.0f;
  float finalLaplacian = 0.0f;

  for (int step = 0; step < numSteps; ++step) {
    finalCorrectionTerm = evaluateQuadraticCorrection(model, currentStencil.centerTempC);
    Phase1StepResult phase1Step = advancePhase1Discretization(currentStencil, alpha, finalCorrectionTerm, gridSpacing, subDt);
    currentStencil.centerTempC = phase1Step.nextTempC;
    finalLaplacian = phase1Step.laplacian;
  }

  return {
      finalLaplacian,
      alpha * finalLaplacian,
      finalCorrectionTerm,
      currentStencil.centerTempC,
  };
}

Phase2TrainingUpdate computePhase2Gradients(const float measuredTempC, const float predictedTempC,
                                            const float inputTempC) {
  const float error = measuredTempC - predictedTempC;

  return {
      error,
      error * inputTempC,
      error * inputTempC * inputTempC,
      error,
  };
}

void applyPhase2AdamUpdate(ModelParameters& model, ModelParameters& m, ModelParameters& v,
                           const uint32_t step, const Phase2TrainingUpdate& grads, const float learningRate) {
  const float beta1 = 0.9f;
  const float beta2 = 0.999f;
  const float epsilon = 1e-8f;

  // Update biased first moment estimate (negative gradient because grads is "error")
  // We want to minimize loss = (predicted - measured)^2, gradient of w is -2 * error * input
  // So grads.deltaW1 = error * input (which is negative half gradient).
  // Thus, the update is w += lr * m_hat.
  
  m.w1 = beta1 * m.w1 + (1.0f - beta1) * grads.deltaW1;
  m.w2 = beta1 * m.w2 + (1.0f - beta1) * grads.deltaW2;
  m.b = beta1 * m.b + (1.0f - beta1) * grads.deltaB;

  v.w1 = beta2 * v.w1 + (1.0f - beta2) * (grads.deltaW1 * grads.deltaW1);
  v.w2 = beta2 * v.w2 + (1.0f - beta2) * (grads.deltaW2 * grads.deltaW2);
  v.b = beta2 * v.b + (1.0f - beta2) * (grads.deltaB * grads.deltaB);

  const float bias1 = 1.0f - powf(beta1, static_cast<float>(step));
  const float bias2 = 1.0f - powf(beta2, static_cast<float>(step));

  const float mHatW1 = m.w1 / bias1;
  const float mHatW2 = m.w2 / bias2;
  const float mHatB = m.b / bias1;

  const float vHatW1 = v.w1 / bias2;
  const float vHatW2 = v.w2 / bias2;
  const float vHatB = v.b / bias2;

  model.w1 += learningRate * mHatW1 / (sqrtf(vHatW1) + epsilon);
  model.w2 += learningRate * mHatW2 / (sqrtf(vHatW2) + epsilon);
  model.b += learningRate * mHatB / (sqrtf(vHatB) + epsilon);
}

void clampPhase2Model(ModelParameters& model, const float maxAbsW1, const float maxAbsW2,
                      const float maxAbsB) {
  model.w1 = clampAbsValue(model.w1, maxAbsW1);
  model.w2 = clampAbsValue(model.w2, maxAbsW2);
  model.b = clampAbsValue(model.b, maxAbsB);
}

}
