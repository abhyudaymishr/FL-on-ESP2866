// Host-only reproducible sanity experiment. No network/hardware performance claim.
#include <array>
#include <cmath>
#include <cstdio>
#include "node_state.h"
#include "node_model.h"
using namespace pifl;
namespace {
constexpr unsigned side=3, count=side*side, trainSteps=400, testSteps=200;
using Field=std::array<float,count>;
int adjacent(unsigned i, unsigned direction) {
  const int x=i%side,y=i/side;
  if(direction==0) return y>0 ? static_cast<int>(i)-side : -1;
  if(direction==1) return y+1<static_cast<int>(side) ? static_cast<int>(i)+side : -1;
  if(direction==2) return x+1<static_cast<int>(side) ? static_cast<int>(i)+1 : -1;
  return x>0 ? static_cast<int>(i)-1 : -1;
}
FiniteDifferenceStencil stencil(const Field& f,unsigned i) {
  float n[4]; uint8_t mask=0;
  for(unsigned d=0;d<4;++d) {
    const int j=adjacent(i,d);
    n[d]=j<0 ? f[i] : f[j];
    if(j<0) mask |= 1u<<d;
  }
  return {f[i],n[0],n[1],n[2],n[3],mask};
}
Field physicalStep(Field f) {
  // Independent simultaneous fine-step reference, ten steps per observation.
  for(unsigned sub=0;sub<10;++sub) {
    Field next{};
    for(unsigned i=0;i<count;++i) {
      double lap=0;
      for(unsigned d=0;d<4;++d) {
        const int j=adjacent(i,d); lap += (j<0 ? f[i] : f[j])-f[i];
      }
      next[i]=static_cast<float>(f[i]+.1*(.15*lap+.02));
    }
    f=next;
  }
  return f;
}
void feed(NodeState& node,const Field& f,unsigned i,uint32_t now) {
  for(unsigned d=0;d<4;++d) {
    const int j=adjacent(i,d);
    node.neighbors[d].nodeId=j<0 ? 0 : j+1;
    if(j>=0) registerNeighborTemperature(node,j+1,f[j],now);
  }
  applyMeasurement(node,f[i],1);
}
struct Score {
  double squared=0,absolute=0;
  void add(float prediction,float target) {
    const double error=prediction-target; squared+=error*error; absolute+=std::fabs(error);
  }
};
}
int main() {
  std::array<NodeState,count> local{},federated{};
  Field field{};
  for(unsigned i=0;i<count;++i) {
    field[i]=25+5*std::cos(3.141592653589793*(i%side)/(side-1))+(i/side);
    initializeNodeState(local[i],i+1,1001+i);
    initializeNodeState(federated[i],i+1,1001+i);
    for(unsigned j=0;j<count;++j) if(i!=j) registerLivePeer(federated[i],{static_cast<uint8_t>(j+1),1,1001+j},1000);
    feed(local[i],field,i,1000);
    feed(federated[i],field,i,1000);
  }
  ModelParameters centralized{0,0,0},m{0,0,0},v{0,0,0}; uint32_t adamStep=0;
  const char* names[]={"persistence","physics_only","local_only","centralized","federated"};
  Score scores[5];
  unsigned rounds=0;
  for(unsigned step=0;step<trainSteps+testSteps;++step) {
    const auto next=physicalStep(field);
    if(step<trainSteps) {
      for(unsigned i=0;i<count;++i) {
        const auto p=predictPhase2NodeStep(stencil(field,i),kAlpha,centralized,1,1);
        const auto grads=computePhase2Gradients(next[i],p);
        if(!applyPhase2AdamUpdate(centralized,m,v,++adamStep,grads,kLearningRate)) return 2;
        clampPhase2Model(centralized,kMaxAbsW1,kMaxAbsW2,kMaxAbsB);
        feed(local[i],next,i,2000+step*1000);
        feed(federated[i],next,i,2000+step*1000);
      }
      if((step+1)%7==0) {
        for(unsigned i=1;i<count;++i) {
          federated[i].localModelRound++;
          const auto packet=buildLocalModelPacket(federated[i],federated[i].localModelRound);
          if(!registerPeerModel(federated[0],packet,2000+step*1000)) return 3;
        }
        AggregationPacket packet{};
        if(!buildAggregatedModel(federated[0],packet)) return 4;
        for(auto& node : federated) if(!applyGlobalModel(node,packet)) return 5;
        ++rounds;
      }
    } else {
      // Teacher-forced one-step evaluation, frozen parameters, chronological holdout.
      for(unsigned i=0;i<count;++i) {
        const auto s=stencil(field,i);
        const float predictions[]={field[i],predictPhase2NodeStep(s,kAlpha,{0,0,0},1,1).predictedTempC,
            predictPhase2NodeStep(s,kAlpha,local[i].model,1,1).predictedTempC,
            predictPhase2NodeStep(s,kAlpha,centralized,1,1).predictedTempC,
            predictPhase2NodeStep(s,kAlpha,federated[i].model,1,1).predictedTempC};
        for(unsigned method=0;method<5;++method) scores[method].add(predictions[method],next[i]);
      }
    }
    field=next;
  }
  std::printf("{\"scenario\":\"synthetic_3x3_insulated_plate\",\"train_steps\":%u,\"test_steps\":%u,\"test_observations\":%u,\"aggregation_rounds\":%u,\"scores\":{",trainSteps,testSteps,testSteps*count,rounds);
  for(unsigned method=0;method<5;++method) {
    const auto& score=scores[method];
    std::printf("%s\"%s\":{\"rmse_c\":%.8g,\"mae_c\":%.8g}",method ? "," : "",names[method],
        std::sqrt(score.squared/(testSteps*count)),score.absolute/(testSteps*count));
  }
  std::printf("}}\n");
}
