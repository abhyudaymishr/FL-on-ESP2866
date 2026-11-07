#include <cmath>
#include <initializer_list>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "node_state.h"
#include "node_model.h"
#include "packet_envelope.h"
using namespace pifl;
namespace {
unsigned checks=0;
void require(bool condition, const char* description) {
  ++checks;
  if (!condition) { std::fprintf(stderr,"FAIL: %s\n",description); std::exit(1); }
}
bool near(float a,float b,float tolerance=1e-4f) { return std::fabs(a-b) <= tolerance; }
void fixture(const char* name, const uint8_t* payload, size_t length, bool last=false) {
  uint8_t wire[kMaxPacketSize+4];
  const size_t n=wrapPacket(payload,length,wire,sizeof(wire),"test-key");
  std::printf("\"%s\":\"",name);
  for (size_t i=0;i<n;++i) std::printf("%02x",wire[i]);
  std::printf(last ? "\"}\n" : "\",");
}
void fixtures() {
  uint8_t b[kMaxPacketSize]; size_t n;
  std::printf("{");
  n=encodeDiscoveryPacket({1,1,101},b,sizeof(b)); fixture("discovery",b,n);
  n=encodeTemperaturePacket(1,26.25f,b,sizeof(b)); fixture("temperature",b,n);
  n=encodeModelPacket({1,{.125f,-.03125f,.5f},7,.1875f,2,0,0,0,101},b,sizeof(b)); fixture("model",b,n);
  n=encodeAggregationPacket({1,{.25f,.0625f,1},.125f,3,101},b,sizeof(b)); fixture("aggregation",b,n,true);
}
void numericalChecks() {
  ModelParameters model{0,0,0},m{0,0,0},v{0,0,0};
  for(unsigned step=1;step<=10;++step) {
    require(applyPhase2AdamUpdate(model,m,v,step,{1,1,1,1},kLearningRate),"Adam finite update");
    require(near(model.w1,model.w2,1e-8f) && near(model.w1,model.b,1e-8f),"Adam bias correction identical per coordinate");
  }
  require(near(model.w2,10*kLearningRate,1e-7f),"Adam constant-gradient reference");
  const float nan=std::numeric_limits<float>::quiet_NaN();
  const auto before=model;
  require(!applyPhase2AdamUpdate(model,m,v,11,{0,nan,0,0},kLearningRate) && model.w1==before.w1,"invalid gradient leaves optimizer unchanged");
  FiniteDifferenceStencil isolated{24,24,24,24,24,15};
  auto p=predictPhase2NodeStep(isolated,.15f,{0,0,1},1,2);
  require(p.valid && near(p.predictedTempC,26),"zero-flux ghosts evolve through substeps");
  p=predictPhase2NodeStep(isolated,0,{0,0,1},1,1);
  require(p.valid && near(p.predictedTempC,25),"zero diffusion still integrates source");
  require(!predictPhase2NodeStep(isolated,-1,{0,0,0},1,1).valid,"negative diffusion rejected");
  require(!predictPhase2NodeStep(isolated,.15f,{0,0,0},0,1).valid,"zero grid spacing rejected");
  require(!predictPhase2NodeStep(isolated,.15f,{0,0,0},1,0).valid,"zero interval rejected");
  require(!predictPhase2NodeStep(isolated,.15f,{0,0,0},1,31).valid,"unbounded interval rejected");
  require(!predictPhase2NodeStep(isolated,1e20f,{0,0,0},1,1).valid,"substep work bounded");
  for (uint8_t mask : {uint8_t(0),uint8_t(5),uint8_t(15)}) {
    FiniteDifferenceStencil stencil{24,23,25,24,26,mask};
    for (float dt : {.5f,2.f,8.f}) {
      ModelParameters reference{.01f,-.0001f,.02f};
      auto prediction=predictPhase2NodeStep(stencil,.15f,reference,1,dt);
      auto grads=computePhase2Gradients(prediction.predictedTempC+1,prediction);
      for(unsigned coordinate=0;coordinate<3;++coordinate) {
        auto plus=reference,minus=reference;
        const float eps=1e-2f;
        float* up=coordinate==0 ? &plus.w1 : coordinate==1 ? &plus.w2 : &plus.b;
        float* down=coordinate==0 ? &minus.w1 : coordinate==1 ? &minus.w2 : &minus.b;
        *up+=eps; *down-=eps;
        const float numerical=(predictPhase2NodeStep(stencil,.15f,plus,1,dt).predictedTempC-
            predictPhase2NodeStep(stencil,.15f,minus,1,dt).predictedTempC)/(2*eps);
        const float analytic=coordinate==0 ? grads.deltaW1 : coordinate==1 ? grads.deltaW2 : grads.deltaB;
        require(near(numerical,analytic,.01f+.002f*std::fabs(analytic)),"finite-difference gradient across intervals and boundaries");
      }
    }
  }
}
void diffusionReferenceChecks() {
  // Independent Fourier-mode solution of the semidiscrete periodic heat equation.
  float errors[2];
  for (unsigned resolution=0;resolution<2;++resolution) {
    const unsigned steps=resolution ? 20 : 10;
    const float dt=1.0f/steps;
    float field[4][4];
    for (unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x)
      field[y][x]=25+std::cos(3.141592653589793f*x/2);
    for(unsigned step=0;step<steps;++step) {
      float next[4][4];
      for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
        const FiniteDifferenceStencil s{field[y][x],field[(y+3)%4][x],field[(y+1)%4][x],
            field[y][(x+1)%4],field[y][(x+3)%4],0};
        const auto p=predictPhase2NodeStep(s,.15f,{0,0,0},1,dt);
        require(p.valid,"periodic heat step remains finite");
        next[y][x]=p.predictedTempC;
      }
      std::memcpy(field,next,sizeof(field));
    }
    float total=0;
    for(auto& row : field) for(float t : row) total+=t;
    require(near(total,400,.002f),"diffusion conserves total temperature on periodic grid");
    errors[resolution]=std::fabs(field[0][0]-(25+std::exp(-.3f)));
  }
  require(errors[1] < .6f*errors[0] && errors[1] > .4f*errors[0],"Euler error converges at first order against analytic mode");
}
void stateChecks() {
  NodeState s{}; initializeNodeState(s,5,505);
  registerNeighborTemperature(s,2,30,1000);
  require(near(computeLaplacian(s),0),"discovery does not invent physical neighbors");
  s.neighbors[0].nodeId=2;
  registerNeighborTemperature(s,2,30,1000);
  require(near(computeLaplacian(s),6),"explicit physical neighbor contributes");
  registerLivePeer(s,{2,0,202},7000); expireStaleData(s,7500);
  require(near(computeLaplacian(s),0) && !s.livePeers[0].hasTemperature,"heartbeat cannot refresh temperature");
  require(electedAggregatorId(s)==5,"ineligible lower ID cannot become leader");
  registerLivePeer(s,{2,1,202},7500);
  require(electedAggregatorId(s)==2,"eligible lower ID elected");
  require(!applyGlobalModel(s,{3,{.1f,0,0},.1f,10,303}),"unelected global sender rejected");
  require(!applyGlobalModel(s,{2,{.1f,0,0},.1f,10,999}),"wrong boot ID rejected");
  require(applyGlobalModel(s,{2,{.1f,0,0},.1f,10,202}),"elected global update accepted");
  require(!applyGlobalModel(s,{2,{-.1f,0,0},.1f,9,202}),"stale global round rejected");
  require(!applyGlobalModel(s,{2,{-.1f,0,0},.1f,10,202}),"duplicate global round rejected");
  require(!applyGlobalModel(s,{2,{std::numeric_limits<float>::quiet_NaN(),0,0},.1f,11,202}),"NaN global parameter rejected");
  registerLivePeer(s,{2,1,203},7600);
  require(applyGlobalModel(s,{2,{.1f,0,0},.1f,1,203}),"new leader boot can restart rounds");
  require(!applyGlobalModel(s,{2,{.1f,0,0},.1f,12,202}),"old leader boot rejected");
  require(isNewerRound(0,65535) && !isNewerRound(65535,0),"round rollover follows serial arithmetic");
  initializeNodeState(s,1,101);
  s.localSampleCount=1; s.localLossSum=.4f; s.model={.1f,0,0};
  registerLivePeer(s,{2,1,202},1000);
  ModelPacket peer{2,{.3f,0,0},3,.8f,1,0,0,0,202};
  require(registerPeerModel(s,peer,1000),"same-base peer model accepted");
  require(!registerPeerModel(s,peer,1001),"duplicate peer update rejected");
  peer.round=2; peer.sampleCount=2;
  require(!registerPeerModel(s,peer,1002),"decreasing sample count rejected");
  AggregationPacket aggregate{};
  require(buildAggregatedModel(s,aggregate),"aggregate available with samples");
  require(near(aggregate.model.w1,.25f) && near(aggregate.meanLocalLoss,.7f),"sample-weighted aggregation reference");
  require(applyGlobalModel(s,aggregate),"leader applies own aggregate");
  require(!buildAggregatedModel(s,aggregate),"consumed updates cannot be reaggregated");
  peer.sampleCount=4; peer.round=3;
  require(!registerPeerModel(s,peer,1100),"old-base model rejected after aggregation");
  require(applyMeasurement(s,24,1),"valid measurement accepted");
  const float oldTemperature=s.currentTempC;
  require(!applyMeasurement(s,std::numeric_limits<float>::quiet_NaN(),1) &&
      s.currentTempC==oldTemperature && !s.hasPendingPrediction,"sensor failure neither trains nor becomes a temperature");
  registerLivePeer(s,{1,1,999},1200);
  require(s.identityConflict && electedAggregatorId(s)==0,"colliding IDs stop participation");
}
void codecChecks() {
  uint8_t raw[kMaxPacketSize]={},wire[kMaxPacketSize+4]={},out[kMaxPacketSize]={};
  const size_t n=encodeModelPacket({1,{.1f,0,0},3,.2f,1,0,0,0,101},raw,sizeof(raw));
  require(n==36,"v2 model payload size");
  const size_t w=wrapPacket(raw,n,wire,sizeof(wire),"test-key");
  size_t length=99;
  require(w==40 && unwrapPacket(wire,w,out,sizeof(out),length,"test-key") && length==n,"bounded envelope round trip");
  uint8_t small[24]; std::memset(small,0x5a,sizeof(small));
  require(!unwrapPacket(wire,w,small,sizeof(small),length,"test-key") && length==0 && small[0]==0x5a,"oversized verified payload rejected before copy");
  uint8_t oversized[100]={};
  require(!unwrapPacket(oversized,sizeof(oversized),out,sizeof(out),length,"test-key"),"oversized datagram rejected");
  require(wrapPacket(raw,n,wire,4,"test-key")==0,"send capacity checked");
  require(!unwrapPacket(wire,w,out,sizeof(out),length,"wrong-key"),"wrong checksum key rejected");
  wire[4]^=1;
  require(!unwrapPacket(wire,w,out,sizeof(out),length,"test-key"),"corruption rejected");
  ModelPacket p{};
  require(decodeModelPacket(raw,n,p) && p.sampleCount==3,"model round trip");
  raw[2]=1; require(!decodeModelPacket(raw,n,p),"obsolete version rejected");
  require(!decodeModelPacket(nullptr,0,p),"empty decode safely rejected");
  require(!isValidModel({0,1,0}) && !isValidTemperature(200),"domain bounds enforced");
}
}
int main(int argc,char** argv) {
  if(argc>1 && std::strcmp(argv[1],"--fixtures")==0) { fixtures(); return 0; }
  numericalChecks(); diffusionReferenceChecks(); stateChecks(); codecChecks();
  std::printf("%u native regression checks passed\n",checks);
}
