
#ifndef V8_HYDROGEN_DEOPT_CHECKS_REMOVE_H_
#define V8_HYDROGEN_DEOPT_CHECKS_REMOVE_H_

#include "src/hydrogen.h"

#define TRACE(x) PrintF x

namespace v8 {
namespace internal {


class HDeoptChecksRemovePhase : public HPhase {
 public:
  explicit HDeoptChecksRemovePhase(HGraph* graph)
      : HPhase("H_Deopt check remove", graph) { }

  void Run() {
    RemoveInstructions(HValue::kCheckMaps);
  }

 private:
  void RemoveInstructions(HValue::Opcode opcode);
  void PrintInst(HValue* instr);
};


} }  // namespace v8::internal

#endif  // V8_HYDROGEN_DEOPT_CHECKS_REMOVE_H_

