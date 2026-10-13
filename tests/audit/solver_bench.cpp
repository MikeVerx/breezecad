#include "sketch/Solver.h"
#include "sketch/Sketch.h"
#include <chrono>
#include <cstdio>
using namespace bz;
static double Ms(std::chrono::steady_clock::time_point a){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-a).count();}
int main(){
  std::printf("-- Perforated plate: N x N circles, each with a diameter and a fixed centre (DIY typical)\n");
  for(int n: {5,10,15,20,25}){
    sketch::Sketch sk; IdGenerator ids; sk.EnsureOrigin(ids);
    for(int i=0;i<n;++i)for(int j=0;j<n;++j){
      ObjectId c=sk.AddPoint(ids,{i*10.0,j*10.0}); ObjectId ci=sk.AddCircle(ids,c,3.0);
      sketch::Constraint d; d.type=sketch::ConstraintType::Diameter; d.refs={ci,{},{}}; d.parameter.value=6.0; sk.AddConstraint(ids,d);
      sketch::Constraint f; f.type=sketch::ConstraintType::Fix; f.refs={c,{},{}}; f.fixedAt={i*10.0+0.5,j*10.0}; sk.AddConstraint(ids,f);
    }
    auto t=std::chrono::steady_clock::now(); auto r=sketch::Solve(sk); double a=Ms(t);
    t=std::chrono::steady_clock::now(); sketch::Solve(sk); double b=Ms(t);
    std::printf("  %2dx%-2d = %4d holes: first solve %8.1f ms, re-solve (every rebuild) %8.1f ms, conv %d\n",n,n,n*n,a,b,r.converged);
  }
  std::printf("-- Chain of rectangles fully dimensioned (H/V + length)\n");
  for(int n:{25,50,100,200}){
    sketch::Sketch sk; IdGenerator ids; sk.EnsureOrigin(ids);
    for(int i=0;i<n;++i){double x=i*30.0; ObjectId p[4]={sk.AddPoint(ids,{x,0}),sk.AddPoint(ids,{x+20,0}),sk.AddPoint(ids,{x+20,10}),sk.AddPoint(ids,{x,10})};
      ObjectId l[4]; for(int k=0;k<4;++k) l[k]=sk.AddLine(ids,p[k],p[(k+1)%4]);
      for(int k=0;k<4;++k){sketch::Constraint c; c.type=k%2?sketch::ConstraintType::Vertical:sketch::ConstraintType::Horizontal; c.refs={l[k],{},{}}; sk.AddConstraint(ids,c);}
      sketch::Constraint w; w.type=sketch::ConstraintType::Length; w.refs={l[0],{},{}}; w.parameter.value=21; sk.AddConstraint(ids,w);}
    auto t=std::chrono::steady_clock::now(); sketch::Solve(sk); double a=Ms(t);
    t=std::chrono::steady_clock::now(); sketch::Solve(sk); double b=Ms(t);
    std::printf("  %4d rects (%4zu constraints): first %8.1f ms, re-solve %8.1f ms\n",n,sk.constraints.size(),a,b);
  }
}
