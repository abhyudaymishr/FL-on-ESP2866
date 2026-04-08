#include "node_model.h"

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
  const float correctionTerm = evaluateQuadraticCorrection(model, stencil.centerTempC);
  const Phase1StepResult phase1Step =
      advancePhase1Discretization(stencil, alpha, correctionTerm, gridSpacing, dtSeconds);

  return {
      phase1Step.laplacian,
      alpha * phase1Step.laplacian,
      correctionTerm,
      phase1Step.nextTempC,
  };
}

Phase2TrainingUpdate computePhase2SgdUpdate(const float measuredTempC, const float predictedTempC,
                                            const float inputTempC, const float learningRate,
                                            const float dtSeconds) {
  const float error = measuredTempC - predictedTempC;
  const float scaledLearningRate = learningRate * dtSeconds;

  return {
      error,
      scaledLearningRate * error * inputTempC,
      scaledLearningRate * error * inputTempC * inputTempC,
      scaledLearningRate * error,
  };
}

void applyPhase2SgdUpdate(ModelParameters& model, const Phase2TrainingUpdate& update) {
  model.w1 += update.deltaW1;
  model.w2 += update.deltaW2;
  model.b += update.deltaB;
}

void clampPhase2Model(ModelParameters& model, const float maxAbsW1, const float maxAbsW2,
                      const float maxAbsB) {
  model.w1 = clampAbsValue(model.w1, maxAbsW1);
  model.w2 = clampAbsValue(model.w2, maxAbsW2);
  model.b = clampAbsValue(model.b, maxAbsB);
}

}
