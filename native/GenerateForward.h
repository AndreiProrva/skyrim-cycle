#pragma once
namespace cycle {
template<class Function,class Clip,class Context,class Output>
void forwardGenerate(Function&& original,Clip* clip,const Context& context,
                     const Output** children,Output& output,float timeOffset) {
    original(clip,context,children,output,timeOffset);
}
}
