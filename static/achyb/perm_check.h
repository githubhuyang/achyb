#ifndef _PERM_CHECK_
#define _PERM_CHECK_

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/Value.h"

#include "aux.h"
#include "commontypes.h"
#include "internal.h"

#include "gating_function_base.h"

using namespace llvm;

/*class PermCheckBase {
protected:
  Module &m;

public:
  PermCheckBase(Module &_m) : m(_m){};
  virtual ~PermCheckBase(){};
  virtual bool is_perm_check(Function *) { return false; };
  virtual bool is_perm_check(std::string &) { return false; };
  virtual void dump(){};
  virtual void dump_interesting(InstructionSet *);
};*/

class PermCheck : public GatingFunctionBase {
protected:
  StringSet name_set; // Yang
  FunctionSet pointer_set; // Yang
  FunctionSet perm_check_set;// Yang

  StringSet block_list;
  bool semiauto;
  
  /*
   * record capability parameter position passed to capability check function
   * all discovered wrapper function to check functions will also have one entry
   *
   * This data is available after calling collect_wrappers()
   */
  //FunctionData chk_func_cap_position;
  //Str2Int perm_check_name2perm_check_arg_pos;

private:
  void load_ac_interfaces(std::string &);

public:
  PermCheck(Module &, std::string &, bool);
  ~PermCheck(){};
  virtual bool is_gating_function(Function *);
  virtual bool is_gating_function(std::string &);
  virtual void dump();
  //virtual void dump_interesting(InstructionSet *);

  // Yang:
  bool is_wrap_equal(Function*, Function*);
  void get_wrapper_tree(FunctionSet&, Function*, int);
  //void backward_call_slicing_insts(InstructionSet&, SpecInstructionSet&, Instruction*);
  //void backward_call_slicing_blocks(InstructionSet&, SpecInstructionSet&, BasicBlock*);
};


#endif //_GATING_FUNCTION_BASE_
