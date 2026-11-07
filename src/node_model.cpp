#include "node_model.h"
#include <math.h>

namespace pifl {
namespace {
bool finiteModel(const ModelParameters& m) {
  return isfinite(m.w1) && isfinite(m.w2) && isfinite(m.b);
}
float clampAbsValue(float value, float bound) {
  return fmaxf(-bound, fminf(value, bound));
}
}

float evaluateQuadraticCorrection(const ModelParameters& model, float temperatureC) {
  const float u = (temperatureC - kFeatureCenterC) / kFeatureScaleC;
  return model.w1 * u + model.w2 * u * u + model.b;
}

Phase2Prediction predictPhase2NodeStep(const FiniteDifferenceStencil& stencil, float alpha,
                                      const ModelParameters& model, float gridSpacing,
                                      float dtSeconds) {
  Phase2Prediction result{0, 0, 0, stencil.centerTempC, {0, 0, 0}, false};
  if (!isfinite(alpha) || alpha < 0 || !isfinite(gridSpacing) || gridSpacing <= 0 ||
      !isfinite(dtSeconds) || dtSeconds <= 0 || dtSeconds > 30 || !finiteModel(model) ||
      !isfinite(stencil.centerTempC) || !isfinite(stencil.northTempC) ||
      !isfinite(stencil.southTempC) || !isfinite(stencil.eastTempC) ||
      !isfinite(stencil.westTempC)) return result;

  // This restriction controls diffusion only, not an arbitrary learned reaction.
  const float maxDt = alpha > 0 ? 0.25f * gridSpacing * gridSpacing / alpha : dtSeconds;
  const float requestedSteps = ceilf(dtSeconds / maxDt);
  if (!isfinite(requestedSteps) || requestedSteps > 256) return result;
  const unsigned steps = requestedSteps < 1 ? 1 : static_cast<unsigned>(requestedSteps);
  const float subDt = dtSeconds / steps;
  unsigned zeroFluxCount = 0;
  for (unsigned i = 0; i < 4; ++i) zeroFluxCount += (stencil.zeroFluxMask >> i) & 1u;
  const float lapDerivative = (static_cast<float>(zeroFluxCount) - 4) /
                              (gridSpacing * gridSpacing);
  FiniteDifferenceStencil current = stencil;
  for (unsigned i = 0; i < steps; ++i) {
    const float t = current.centerTempC;
    const float u = (t - kFeatureCenterC) / kFeatureScaleC;
    const float jacobian = 1 + subDt * (alpha * lapDerivative +
        (model.w1 + 2 * model.w2 * u) / kFeatureScaleC);
    result.sensitivity = {
        jacobian * result.sensitivity.w1 + subDt * u,
        jacobian * result.sensitivity.w2 + subDt * u * u,
        jacobian * result.sensitivity.b + subDt};
    result.laplacian = computeFiniteDifferenceLaplacian(current, gridSpacing);
    result.diffusionTerm = alpha * result.laplacian;
    result.correctionTerm = evaluateQuadraticCorrection(model, t);
    current.centerTempC = explicitEulerStep(t, alpha, result.laplacian,
                                          result.correctionTerm, subDt);
    if (!isfinite(current.centerTempC) || !finiteModel(result.sensitivity)) return result;
  }
  result.predictedTempC = current.centerTempC;
  result.valid = true;
  return result;
}

Phase2TrainingUpdate computePhase2Gradients(float measuredTempC, const Phase2Prediction& prediction) {
  if (!prediction.valid || !isfinite(measuredTempC)) return {0, 0, 0, 0};
  const float error = measuredTempC - prediction.predictedTempC;
  // Negative gradient of half squared error, including every Euler substep.
  return {error, error * prediction.sensitivity.w1, error * prediction.sensitivity.w2,
          error * prediction.sensitivity.b};
}

bool applyPhase2AdamUpdate(ModelParameters& model, ModelParameters& m, ModelParameters& v,
                          uint32_t step, const Phase2TrainingUpdate& grads, float learningRate) {
  if (step == 0 || !isfinite(learningRate) || learningRate <= 0 || !finiteModel(model) ||
      !finiteModel(m) || !finiteModel(v) || v.w1 < 0 || v.w2 < 0 || v.b < 0 ||
      !isfinite(grads.deltaW1) || !isfinite(grads.deltaW2) || !isfinite(grads.deltaB)) return false;
  const float beta1 = 0.9f, beta2 = 0.999f, epsilon = 1e-8f;
  const ModelParameters nextM{beta1*m.w1 + (1-beta1)*grads.deltaW1,
                              beta1*m.w2 + (1-beta1)*grads.deltaW2,
                              beta1*m.b + (1-beta1)*grads.deltaB};
  const ModelParameters nextV{beta2*v.w1 + (1-beta2)*grads.deltaW1*grads.deltaW1,
                              beta2*v.w2 + (1-beta2)*grads.deltaW2*grads.deltaW2,
                              beta2*v.b + (1-beta2)*grads.deltaB*grads.deltaB};
  const float bias1 = 1-powf(beta1, static_cast<float>(step));
  const float bias2 = 1-powf(beta2, static_cast<float>(step));
  const ModelParameters candidate{
      model.w1 + learningRate*(nextM.w1/bias1)/(sqrtf(nextV.w1/bias2)+epsilon),
      model.w2 + learningRate*(nextM.w2/bias1)/(sqrtf(nextV.w2/bias2)+epsilon),
      model.b + learningRate*(nextM.b/bias1)/(sqrtf(nextV.b/bias2)+epsilon)};
  if (!finiteModel(candidate) || !finiteModel(nextM) || !finiteModel(nextV)) return false;
  model = candidate;
  m = nextM;
  v = nextV;
  return true;
}

void clampPhase2Model(ModelParameters& model, float maxAbsW1, float maxAbsW2, float maxAbsB) {
  model.w1 = clampAbsValue(model.w1, maxAbsW1);
  model.w2 = clampAbsValue(model.w2, maxAbsW2);
  model.b = clampAbsValue(model.b, maxAbsB);
}
}
