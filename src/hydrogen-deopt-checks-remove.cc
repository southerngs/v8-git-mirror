

#include "src/hydrogen-deopt-checks-remove.h"
#include "src/v8.h"

namespace v8 {
namespace internal {

void HDeoptChecksRemovePhase::PrintInst(HValue* instr) {
  OFStream os(stdout);
  os << *instr;
#if 0
  os << "[DeoptChecks remove: ";
  os << "'" << *instr << "' ";
  os << "no uses: " << instr->HasNoUses() << std::endl;
#endif

}

void HDeoptChecksRemovePhase::RemoveInstructions(HValue::Opcode opcode) {

#if 0
  for (int i = 0; i < graph()->blocks()->length(); ++i) {
    HBasicBlock* block = graph()->blocks()->at(i);
    for (HInstructionIterator it(block); !it.Done(); it.Advance()) {
      HInstruction* instr = it.Current();
      if (instr->opcode() == opcode) {
        // Print instruction for now
        PrintInst(instr);
        //instr->DeleteAndReplaceWith(NULL);
        if(!FLAG_remove_deopt_checks_print_only) {
          HValue* value = instr->ActualValue();
          instr->DeleteAndReplaceWith(value);
        }
        //instr->SetFlag(HValue::kIsDead);
      }
    }
  }
#endif
  OFStream os(stdout);
  for (int i = 0; i < graph()->blocks()->length(); ++i) {
    HBasicBlock* block = graph()->blocks()->at(i);
    for (HInstructionIterator it(block); !it.Done(); it.Advance()) {
      HInstruction* instr = it.Current();
      //if (instr->opcode() == opcode) {
      if(instr->CannotBeEliminated()) {
        //os << instr->CannotBeEliminated() << " ";
        if(instr != NULL) {
          os << *instr << std::endl;
        }
      }
      //}
    }
  }

}

} }  // namespace v8::internal

