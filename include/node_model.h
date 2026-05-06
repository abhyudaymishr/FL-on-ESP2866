#pragma once

#include "discretization.h"
#include "pifl_types.h"

namespace pifl {

struct Phase2Prediction {
  float laplacian;
  float diffusionTerm;
  float correctionTerm;
  float predictedTempC;
};

struct Phase2TrainingUpdate {
  float error;
  float deltaW1;
  float deltaW2;
  float deltaB;
};

float evaluateQuadraticCorrection(const ModelParameters& model, float temperatureC);
Phase2Prediction predictPhase2NodeStep(const FiniteDifferenceStencil& stencil, float alpha,
                                       const ModelParameters& model, float gridSpacing,
                                       float dtSeconds);
Phase2TrainingUpdate computePhase2Gradients(float measuredTempC, float predictedTempC,
                                            float inputTempC);
void applyPhase2AdamUpdate(ModelParameters& model, ModelParameters& m, ModelParameters& v,
                           uint32_t step, const Phase2TrainingUpdate& grads, float learningRate);
void clampPhase2Model(ModelParameters& model, float maxAbsW1, float maxAbsW2, float maxAbsB);

}
