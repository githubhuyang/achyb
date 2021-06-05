#include "achyb.h"

#include "cvfa.h"
// my aux headers
#include "color.h"
#include "stopwatch.h"
#include "utility.h"

#define TOTOAL_NUMBER_OF_STOP_WATCHES 2
#define WID_0 0
#define WID_KINIT 1
#define WID_CC 1
#define WID_PI 1

STOP_WATCH(TOTOAL_NUMBER_OF_STOP_WATCHES);

using namespace llvm;

#include "capstat.h"
#include "knobs.h"

#include "module_duplicator.h"

#include <mutex>
#include <pthread.h>
#include <thread>

char achyb::ID;
Instruction *x_dbg_ins;
std::list<int> x_dbg_idx;

std::mutex x_lock;

////////////////////////////////////////////////////////////////////////////////

/*
 * deal with struct name alias
 */
void achyb::find_in_mi2m(Type *t, ModuleSet &ms) {
  ms.clear();
  StructType *st = dyn_cast<StructType>(t);
  if (!st) {
    // t->print(errs());
    // errs()<<"\n";
    return;
  }
  assert(st);
  if (!st->hasName()) {
    if (mi2m.find(t) != mi2m.end())
      for (auto i : *mi2m[t])
        ms.insert(i);
    return;
  }
  // match using struct name
  std::string name = t->getStructName();
  str_truncate_dot_number(name);
  for (auto msi : mi2m) {
    StructType *stype = dyn_cast<StructType>(msi.first);
    if (!stype->hasName())
      continue;
    std::string struct_name = stype->getName();
    str_truncate_dot_number(struct_name);
    if (struct_name != name)
      continue;
    for (auto i : (*msi.second)) {
      ms.insert(i);
    }
  }
}
/*
 * interesting type which contains functions pointers to deal with user request
 */
bool achyb::is_interesting_type(Type *ty) {
  if (!ty->isStructTy())
    return false;
  if (!dyn_cast<StructType>(ty)->hasName())
    return false;
  StringRef tyn = ty->getStructName();
  for (int i = 0; i < BUILTIN_INTERESTING_TYPE_WORD_LIST_SIZE; i++) {
    if (tyn.startswith(_builtin_interesting_type_word[i]))
      return true;
  }
  if (discovered_interesting_type.count(ty) != 0)
    return true;
  return false;
}
bool achyb::_is_used_by_static_assign_to_interesting_type(
    Value *v, std::unordered_set<Value *> &duchain) {
  if (duchain.count(v))
    return false;
  duchain.insert(v);
  if (is_interesting_type(v->getType())) {
    duchain.erase(v);
    return true;
  }
  for (auto *u : v->users()) {
    if (isa<Instruction>(u))
      continue;
    if (_is_used_by_static_assign_to_interesting_type(u, duchain)) {
      duchain.erase(v);
      return true;
    }
  }
  duchain.erase(v);
  return false;
}

bool achyb::is_used_by_static_assign_to_interesting_type(Value *v) {
  std::unordered_set<Value *> duchain;
  return _is_used_by_static_assign_to_interesting_type(v, duchain);
}

////////////////////////////////////////////////////////////////////////////////
/*
 * debug function
 */
void achyb::dump_as_good(InstructionList &callstk) {
  if (!knob_dump_good_path)
    return;
  errs() << ANSI_COLOR_MAGENTA << "Use:";
  callstk.front()->getDebugLoc().print(errs());
  errs() << ANSI_COLOR_RESET;
  errs() << "\n"
         << ANSI_COLOR_GREEN << "=GOOD PATH=" << ANSI_COLOR_RESET << "\n";
  dump_callstack(callstk);
}

void achyb::dump_as_bad(InstructionList &callstk) {
  if (!knob_dump_bad_path)
    return;
  errs() << ANSI_COLOR_MAGENTA << "Use:";
  callstk.front()->getDebugLoc().print(errs());
  errs() << ANSI_COLOR_RESET;
  errs() << "\n" << ANSI_COLOR_RED << "=BAD PATH=" << ANSI_COLOR_RESET << "\n";
  
  dump_callstack(callstk);
  
  pthread_mutex_lock(&my_lock);
  auto last_inst = callstk.front();
  bad_set.insert(last_inst->getParent()->getParent());
  pthread_mutex_unlock(&my_lock);
  // Yang: comment this as it can slow donw the analysis
  // dump_a_path(callstk);
}

void achyb::dump_as_ignored(InstructionList &callstk) {
  if (!knob_dump_ignore_path)
    return;
  errs() << ANSI_COLOR_MAGENTA << "Use:";
  callstk.front()->getDebugLoc().print(errs());
  errs() << ANSI_COLOR_RESET;
  errs() << "\n"
         << ANSI_COLOR_YELLOW << "=IGNORE PATH=" << ANSI_COLOR_RESET << "\n";
  dump_callstack(callstk);
}

void achyb::dump_v2ci() {
  if (!knob_achyb_v2c)
    return;
  errs() << ANSI_COLOR(BG_BLUE, FG_WHITE)
         << "--- Variables Protected By Gating Function---" << ANSI_COLOR_RESET
         << "\n";
  for (auto &cis : v2ci) {
    Value *v = cis.first;
    errs() << ANSI_COLOR_GREEN << v->getName() << ANSI_COLOR_RESET << "\n";
    gating->dump_interesting(cis.second);
  }
}

void achyb::dump_f2ci() {
  if (!knob_achyb_f2c)
    return;
  errs() << ANSI_COLOR(BG_BLUE, FG_WHITE)
         << "--- Function Protected By Gating Function---" << ANSI_COLOR_RESET
         << "\n";
  for (auto &cis : f2ci) {
    Function *func = cis.first;
    errs() << ANSI_COLOR_GREEN << func->getName() << ANSI_COLOR_RESET << "\n";
    gating->dump_interesting(cis.second);
  }
}

/*
 * dump interesting type field and guarding checks
 */
void achyb::dump_tf2ci() {
  if (!knob_achyb_t2c)
    return;

  errs() << ANSI_COLOR(BG_CYAN, FG_WHITE)
         << "--- Interesting Type fields and checks ---" << ANSI_COLOR_RESET
         << "\n";
  for (auto &cis : t2ci) {
    StructType *t = dyn_cast<StructType>(cis.first);
    if (t->hasName())
      errs() << ANSI_COLOR_GREEN << t->getName();
    else
      errs() << ANSI_COLOR_RED << "AnnonymouseType";
    errs() << ":";
    std::unordered_set<int> &fields = critical_typefields[t];
    for (auto i : fields)
      errs() << i << ",";
    errs() << ANSI_COLOR_RESET << "\n";
    gating->dump_interesting(cis.second);
  }
}

void achyb::dump_kinit() {
  if (!knob_achyb_kinit)
    return;
  errs() << ANSI_COLOR(BG_BLUE, FG_WHITE)
         << "=Kernel Init Functions=" << ANSI_COLOR_RESET << "\n";
  for (auto I : kernel_init_functions) {
    errs() << I->getName() << "\n";
  }
  errs() << "=o=\n";
}

void achyb::dump_non_kinit() {
  if (!knob_achyb_nkinit)
    return;
  errs() << ANSI_COLOR(BG_BLUE, FG_WHITE)
         << "=NON-Kernel Init Functions=" << ANSI_COLOR_RESET << "\n";
  for (auto I : non_kernel_init_functions) {
    errs() << I->getName() << "\n";
  }
  errs() << "=o=\n";
}

void achyb::dump_gating() {
  if (!knob_achyb_caw)
    return;
  gating->dump();
}

void achyb::dump_kmi() {
  if (!knob_achyb_kmi)
    return;
  errs() << ANSI_COLOR(BG_BLUE, FG_WHITE)
         << "=Kernel Module Interfaces=" << ANSI_COLOR_RESET << "\n";
  for (auto msi : mi2m) {
    StructType *stype = dyn_cast<StructType>(msi.first);
    if (stype->hasName())
      errs() << ANSI_COLOR_RED << stype->getName() << ANSI_COLOR_RESET << "\n";
    else
      errs() << ANSI_COLOR_RED << "AnnonymouseType" << ANSI_COLOR_RESET << "\n";
    for (auto m : (*msi.second)) {
      if (m->hasName())
        errs() << "    " << ANSI_COLOR_CYAN << m->getName() << ANSI_COLOR_RESET
               << "\n";
      else
        errs() << "    " << ANSI_COLOR_CYAN << "Annoymous" << ANSI_COLOR_RESET
               << "\n";
    }
  }
  errs() << "=o=\n";
}

////////////////////////////////////////////////////////////////////////////////
/*
 * is this function type contains non-trivial(non-primary) type?
 */
bool achyb::is_complex_type(Type *t) {
  if (!t->isFunctionTy())
    return false;
  if (t->isFunctionVarArg())
    return true;
  FunctionType *ft = dyn_cast<FunctionType>(t);
  // params
  int number_of_complex_type = 0;
  for (int i = 0; i < (int)ft->getNumParams(); i++) {
    Type *argt = ft->getParamType(i);
  strip_pointer:
    if (argt->isPointerTy()) {
      argt = argt->getPointerElementType();
      goto strip_pointer;
    }

    if (argt->isSingleValueType())
      continue;
    number_of_complex_type++;
  }
  // return type
  Type *rt = ft->getReturnType();

again: // to strip pointer
  if (rt->isPointerTy()) {
    Type *pet = rt->getPointerElementType();
    if (pet->isPointerTy()) {
      rt = pet;
      goto again;
    }
    if (!pet->isSingleValueType()) {
      number_of_complex_type++;
    }
  }

  return (number_of_complex_type != 0);
}

/*
 * def/use global?
 * take care of phi node using `visited'
 */
Value *achyb::get_global_def(Value *val, ValueSet &visited) {
  if (visited.count(val) != 0)
    return NULL;
  visited.insert(val);
  if (isa<GlobalValue>(val))
    return val;
  if (Instruction *vali = dyn_cast<Instruction>(val)) {
    for (auto &U : vali->operands()) {
      Value *v = get_global_def(U, visited);
      if (v)
        return v;
    }
  } /*else if (Value* valv = dyn_cast<Value>(val))
   {
       //llvm_unreachable("how can this be ?");
   }*/
  return NULL;
}

Value *achyb::get_global_def(Value *val) {
  ValueSet visited;
  return get_global_def(val, visited);
}

bool achyb::is_rw_global(Value *val) {
  ValueSet visited;
  return get_global_def(val, visited) != NULL;
}

/*
 * is this functions part of the kernel init sequence?
 * if function f has single user which goes to start_kernel(),
 * then this is a init function
 */
bool achyb::is_kernel_init_functions(Function *f, FunctionSet &visited) {
  if (kernel_init_functions.count(f) != 0)
    return true;
  if (non_kernel_init_functions.count(f) != 0)
    return false;

  // init functions with initcall prefix belongs to kernel init sequence
  if (function_has_gv_initcall_use(f)) {
    kernel_init_functions.insert(f);
    return true;
  }

  // not found in cache?
  // all path that can reach to f should start from start_kernel()
  // look backward(find who used f)
  FunctionList flist;
  for (auto *U : f->users())
    if (CallInst *cs = dyn_cast<CallInst>(U))
      flist.push_back(cs->getFunction());

  // no user?
  if (flist.size() == 0) {
    non_kernel_init_functions.insert(f);
    return false;
  }

  visited.insert(f);
  while (flist.size()) {
    Function *xf = flist.front();
    flist.pop_front();
    if (visited.count(xf))
      continue;
    visited.insert(xf);
    if (!is_kernel_init_functions(xf, visited)) {
      non_kernel_init_functions.insert(f);
      return false;
    }
  }
  kernel_init_functions.insert(f);
  return true;
}

bool achyb::is_kernel_init_functions(Function *f) {
  FunctionSet visited;
  return is_kernel_init_functions(f, visited);
}

void achyb::collect_kernel_init_functions(Module &module) {
  // kstart is the first function in boot sequence
  Function *kstart = NULL;
  // kernel init functions
  FunctionSet kinit_funcs;
  // Step 1: find kernel entry point
  // errs() << "Finding Kernel Entry Point and all __initcall_\n";
  // STOP_WATCH_START(WID_KINIT);
  for (Module::iterator fi = module.begin(), f_end = module.end(); fi != f_end;
       ++fi) {
    Function *func = dyn_cast<Function>(fi);
    if (func->isDeclaration() || func->isIntrinsic() || (!func->hasName()))
      continue;
    StringRef fname = func->getName();
    if (fname.startswith("x86_64_start_kernel")) {
      // errs() << ANSI_COLOR_GREEN << "Found " << func->getName()
      //       << ANSI_COLOR_RESET << "\n";
      kstart = func;
      kinit_funcs.insert(kstart);
      kernel_init_functions.insert(func);
    } else if (fname.startswith("start_kernel")) {
      // we should consider start_kernel as kernel init functions no
      // matter what
      kernel_init_functions.insert(func);
      kinit_funcs.insert(func);
      if (kstart == NULL)
        kstart = func;
      // everything calling start_kernel should be considered init
      // for (auto *U: func->users())
      //    if (Instruction *I = dyn_cast<Instruction>(U))
      //        kinit_funcs.insert(I->getFunction());
    } else {
      if (function_has_gv_initcall_use(func))
        kernel_init_functions.insert(func);
    }
  }
  // should always find kstart
  if (kstart == NULL) {
    // errs()
    //    << ANSI_COLOR_RED
    //    << "kstart function not found, may affect precission, continue anyway\n"
    //    << ANSI_COLOR_RESET;
  }
  // STOP_WATCH_STOP(WID_KINIT);
  // STOP_WATCH_REPORT(WID_KINIT);

  // errs() << "Initial Kernel Init Function Count:"
  //       << kernel_init_functions.size() << "\n";

  // Step 2: over approximate kernel init functions
  // errs() << "Over Approximate Kernel Init Functions\n";
  // STOP_WATCH_START(WID_KINIT);
  FunctionSet func_visited;
  FunctionSet func_work_set;
  for (auto f : kernel_init_functions)
    func_work_set.insert(f);

  while (func_work_set.size()) {
    Function *cfunc = *func_work_set.begin();
    func_work_set.erase(cfunc);

    if (cfunc->isDeclaration() || cfunc->isIntrinsic() || is_syscall(cfunc))
      continue;

    kinit_funcs.insert(cfunc);
    func_visited.insert(cfunc);
    kernel_init_functions.insert(cfunc);

    // explore call graph starting from this function
    for (Function::iterator fi = cfunc->begin(), fe = cfunc->end(); fi != fe;
         ++fi) {
      BasicBlock *bb = dyn_cast<BasicBlock>(fi);
      for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie;
           ++ii) {
        CallInst *ci = dyn_cast<CallInst>(ii);
        if ((!ci) || (ci->isInlineAsm()))
          continue;
        if (Function *nf = get_callee_function_direct(ci)) {
          if (nf->isDeclaration() || nf->isIntrinsic() ||
              func_visited.count(nf) || is_syscall(nf))
            continue;
          func_work_set.insert(nf);
        } else {
#if 0
                    //indirect call?
                    FunctionSet fs = resolve_indirect_callee(ci);
                    errs()<<"Indirect Call in kernel init seq: @ ";
                    ci->getDebugLoc().print(errs());
                    errs()<<"\n";
                    for (auto callee: fs)
                    {
                        errs()<<"    "<<callee->getName()<<"\n";
                        if (!func_visited.count(callee))
                            func_work_set.insert(callee);
                    }
#endif
        }
      }
    }
  }
  // STOP_WATCH_STOP(WID_KINIT);
  // STOP_WATCH_REPORT(WID_KINIT);

  // errs() << "Refine Result\n";
  // STOP_WATCH_START(WID_KINIT);
  /*
   * ! BUG: query use of inlined function result in in-accurate result?
   * inlined foo();
   * bar(){zoo()} zoo(){foo};
   *
   * query user{foo()} -> zoo()
   * query BasicBlocks in bar -> got call instruction in bar()?
   */
  // remove all non_kernel_init_functions from kernel_init_functions
  // purge all over approximation
  int last_count = 0;

again:
  for (auto f : kinit_funcs) {
    if ((f->getName() == "start_kernel") ||
        (f->getName() == "x86_64_start_kernel") ||
        function_has_gv_initcall_use(f))
      continue;
    for (auto *U : f->users()) {
      CallInstSet cil;
      get_callsite_inst(U, cil);
      bool should_break = false;
      for (auto cs : cil) {
        if (kinit_funcs.count(cs->getFunction()) == 0) {
          // means that we have a user does not belong to kernel init functions
          // we need to remove it
          non_kernel_init_functions.insert(f);
          should_break = true;
          break;
        }
      }
      if (should_break)
        break;
    }
  }
  for (auto f : non_kernel_init_functions) {
    kernel_init_functions.erase(f);
    kinit_funcs.erase(f);
  }

  if (last_count != (int)non_kernel_init_functions.size()) {
    last_count = non_kernel_init_functions.size();
    static int refine_pass = 0;
    // errs() << "refine pass " << refine_pass << " "
    //       << kernel_init_functions.size() << " left\n";
    refine_pass++;
    goto again;
  }

  // errs() << " Refine result : count=" << kernel_init_functions.size() << "\n";
  // STOP_WATCH_STOP(WID_KINIT);
  // STOP_WATCH_REPORT(WID_KINIT);

  // dump_kinit();
}

FunctionSet achyb::resolve_indirect_callee_ldcst_kmi(CallInst *ci, int &err,
                                                      int &kmi_cnt,
                                                      int &dkmi_cnt) {
  FunctionSet fs;
  // non-gep case. loading from bitcasted struct address
  if (StructType *ldbcstty = identify_ld_bcst_struct(ci->getCalledValue())) {
#if 0
        errs()<<"Found ld+bitcast sty to ptrty:";
        if (ldbcstty->isLiteral())
            errs()<<"Literal, ";
        else
            errs()<<ldbcstty->getName()<<", ";
#endif
    // dump_kmi_info(ci);
    Indices indices;
    indices.push_back(0);
    err = 2; // got type
    // match - kmi
    ModuleSet ms;
    find_in_mi2m(ldbcstty, ms);
    if (ms.size()) {
      err = 1; // found module object
      for (auto m : ms)
        if (Value *v = get_value_from_composit(m, indices)) {
          Function *f = dyn_cast<Function>(v);
          assert(f);
          fs.insert(f);
        }
    }
    if (fs.size() != 0) {
      kmi_cnt++;
      goto end;
    }
    // match - dkmi
    if (dmi_type_exists(ldbcstty, dmi))
      err = 1;

    indices.clear();
    indices.push_back(0);
    indices.push_back(0);
    if (FunctionSet *_fs = dmi_exists(ldbcstty, indices, dmi)) {
      for (auto *f : *_fs)
        fs.insert(f);
      dkmi_cnt++;
      goto end;
    }
#if 0
        errs()<<"Try rkmi\n";
#endif
  }
end:
  if (fs.size())
    err = 0;
  return fs;
}

// method 3, improved accuracy
FunctionSet achyb::resolve_indirect_callee_using_kmi(CallInst *ci, int &err) {
  FunctionSet fs;
  Value *cv = ci->getCalledValue();

  err = 6;
  // GEP case.
  // need to find till gep is exhausted and mi2m doesn't have a match
  InstructionSet geps = get_load_from_gep(cv);
  for (auto _gep : geps) {
    GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(_gep);
    Type *cvt =
        dyn_cast<PointerType>(gep->getPointerOperandType())->getElementType();
    if (!cvt->isAggregateType())
      continue;

    Indices indices;
    x_dbg_ins = gep;
    get_gep_indicies(gep, indices);
    x_dbg_idx = indices;
    assert(indices.size() != 0);
    // should remove first element because it is an array index
    // the actual match
    indices.pop_front();
    while (1) {
      if (err > 2)
        err = 2; // found the type, going to match module
      ModuleSet ms;
      find_in_mi2m(cvt, ms);
      if (ms.size()) {
        if (err > 1)
          err = 1; // found matching module
        for (auto m : ms) {
          Value *v = get_value_from_composit(m, indices);
          if (v == NULL) {
            /*
             * NOTE: some of the method may not be implemented
             *       it is ok to ignore them
             * for example: .release method in
             *      struct tcp_congestion_ops
             */
#if 0
                        errs()<<m->getName();
                        errs()<<" - can not get value from composit [ ";
                        for (auto i: indices)
                            errs()<<","<<i;
                        errs()<<"], this method may not implemented yet.\n";
#endif
            continue;
          }
          Function *f = dyn_cast<Function>(v);
          assert(f);
          fs.insert(f);
        }
        break;
      }
      // not found in mi2m
      if (indices.size() <= 1) {
        // no match! we are also done here, mark it as resolved anyway
        // this object may be dynamically allocated,
        // try dkmi if possible
#if 0
                errs()<<" MIDC err, try DKMI\n";
                cvt = get_load_from_type(cv);
                errs()<<"!!!  : ";
                cvt->print(errs());
                errs()<<"\n";
                
                errs()<<"idcs:";
                for (auto i: x_dbg_idx)
                    errs()<<","<<i;
                errs()<<"\n";
                //gep->print(errs());
                errs()<<"\n";
#endif
        break;
      }
      // no match, we can try inner element
      // deal with array of struct here
      Type *ncvt;
      if (ArrayType *aty = dyn_cast<ArrayType>(cvt)) {
        ncvt = aty->getElementType();
        // need to remove another one index
        indices.pop_front();
      } else {
        int idc = indices.front();
        indices.pop_front();
        if (!cvt->isStructTy()) {
          cvt->print(errs());
          llvm_unreachable("!!!1");
        }
        ncvt = cvt->getStructElementType(idc);
        // FIXME! is this correct?
        if (PointerType *pty = dyn_cast<PointerType>(ncvt)) {
          ncvt = pty->getElementType();
          llvm_unreachable("can't be a pointer!");
        }

        // cvt should be aggregated type!
        if (!ncvt->isAggregateType()) {
          /* bad cast!!!
           * struct sk_buff { cb[48] }
           * XFRM_TRANS_SKB_CB(__skb) ((struct xfrm_trans_cb
           * *)&((__skb)->cb[0]))
           */
          // errs()<<"Can not resolve\n";
          // x_dbg_ins->getDebugLoc().print(errs());
          // errs()<<"\n";
          errs() << ANSI_COLOR_RED << "Bad cast from type:" << ANSI_COLOR_RESET;
          ncvt->print(errs());
          errs() << " we can not resolve this\n";
          // dump_kmi_info(ci);
          // llvm_unreachable("NOT POSSIBLE!");
          err = 5;
          break;
        }
      }
      cvt = ncvt;
    }
  }
  if (fs.size() == 0) {
    if (!isa<Instruction>(cv))
      err = 3;
    else if (load_from_global_fptr(cv))
      err = 4;
  } else
    err = 0;
  return fs;
}

/*
 * this is also kmi, but dynamic one
 */
FunctionSet achyb::resolve_indirect_callee_using_dkmi(CallInst *ci, int &err) {
  FunctionSet fs;
  Value *cv = ci->getCalledValue();
  InstructionSet geps = get_load_from_gep(cv);

  err = 6;
  for (auto *_gep : geps) {
    GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(_gep);
    Type *cvt =
        dyn_cast<PointerType>(gep->getPointerOperandType())->getElementType();
    if (!cvt->isAggregateType())
      continue;

    Indices indices;
    // need to find till gep is exhausted and mi2m doesn't have a match
    x_dbg_ins = gep;
    get_gep_indicies(gep, indices);
    x_dbg_idx = indices;
    assert(indices.size() != 0);
    // dig till we are at struct type
    while (1) {
      if (isa<StructType>(cvt))
        break;
      // must be an array
      if (ArrayType *aty = dyn_cast<ArrayType>(cvt)) {
        cvt = aty->getElementType();
        // need to remove another one index
        indices.pop_front();
      } else {
        // no struct inside it and all of them are array?
#if 0
                errs()<<"All array?:";
                cvt->print(errs());
                errs()<<"\n";
#endif
        break;
      }
    }
    if (!dyn_cast<StructType>(cvt))
      continue;
    if (err > 2)
      err = 2;
    if (dmi_type_exists(dyn_cast<StructType>(cvt), dmi) && (err > 1))
      err = 1;
    // OK. now we match through struct type and indices
    if (FunctionSet *_fs =
            dmi_exists(dyn_cast<StructType>(cvt), indices, dmi)) {
      // TODO:iteratively explore basic element type if current one is not found
      if (_fs->size() == 0) {
        // dump_kmi_info(ci);
        errs() << "uk-idcs:";
        if (!dyn_cast<StructType>(cvt)->isLiteral())
          errs() << cvt->getStructName();
        errs() << " [";
        for (auto i : x_dbg_idx)
          errs() << "," << i;
        errs() << "]\n";
      }
      // merge _fs into fs
      for (auto *f : *_fs)
        fs.insert(f);
    }
  }
  if (fs.size())
    err = 0;
  return fs;
}

bool achyb::load_from_global_fptr(Value *cv) {
  ValueList worklist;
  ValueSet visited;
  worklist.push_back(cv);
  int cnt = 0;
  while (worklist.size() && (cnt++ < 5)) {
    Value *v = worklist.front();
    worklist.pop_front();
    if (visited.count(v))
      continue;
    visited.insert(v);

    if (isa<GlobalVariable>(v))
      return true;

    if (isa<Function>(v) || isa<GetElementPtrInst>(v) || isa<CallInst>(v))
      continue;

    if (LoadInst *li = dyn_cast<LoadInst>(v)) {
      worklist.push_back(li->getPointerOperand());
      continue;
    }
    if (SelectInst *sli = dyn_cast<SelectInst>(v)) {
      worklist.push_back(sli->getTrueValue());
      worklist.push_back(sli->getFalseValue());
      continue;
    }
    if (PHINode *phi = dyn_cast<PHINode>(v)) {
      for (unsigned int i = 0; i < phi->getNumIncomingValues(); i++)
        worklist.push_back(phi->getIncomingValue(i));
      continue;
    }
    // instruction
    if (Instruction *i = dyn_cast<Instruction>(v))
      for (unsigned int j = 0; j < i->getNumOperands(); j++)
        worklist.push_back(i->getOperand(j));
    // constant value
    if (ConstantExpr *cxpr = dyn_cast<ConstantExpr>(v))
      worklist.push_back(cxpr->getAsInstruction());
  }
  return false;
}

void achyb::dump_kmi_info(CallInst *ci) {
  Value *cv = ci->getCalledValue();
  ValueList worklist;
  ValueSet visited;
  worklist.push_back(cv);
  int cnt = 0;
  ci->print(errs());
  errs() << "\n";
  while (worklist.size() && (cnt++ < 5)) {
    Value *v = worklist.front();
    worklist.pop_front();
    if (visited.count(v))
      continue;
    visited.insert(v);
    if (isa<Function>(v))
      errs() << v->getName();
    else
      v->print(errs());
    errs() << "\n";
    if (LoadInst *li = dyn_cast<LoadInst>(v)) {
      worklist.push_back(li->getPointerOperand());
      continue;
    }
    if (SelectInst *sli = dyn_cast<SelectInst>(v)) {
      worklist.push_back(sli->getTrueValue());
      worklist.push_back(sli->getFalseValue());
      continue;
    }
    if (PHINode *phi = dyn_cast<PHINode>(v)) {
      for (unsigned int i = 0; i < phi->getNumIncomingValues(); i++)
        worklist.push_back(phi->getIncomingValue(i));
      continue;
    }
    if (isa<CallInst>(v))
      continue;
    if (Instruction *i = dyn_cast<Instruction>(v))
      for (unsigned int j = 0; j < i->getNumOperands(); j++)
        worklist.push_back(i->getOperand(j));
  }
}

/*
 * create mapping for
 *  indirect call site -> callee
 *  callee -> indirect call site
 */
void achyb::populate_indcall_list_through_kmi(Module &module) {
  // indirect call is load+gep and can be found in mi2m?
  int count = 0;
  int targets = 0;
  int fpar_cnt = 0;
  int gptr_cnt = 0;
  int cast_cnt = 0;
  int container_of_cnt = 0;
  ;
  int undefined_1 = 0;
  int undefined_2 = 0;
  int unknown = 0;
  int kmi_cnt = 0;
  int dkmi_cnt = 0;
#if 0
    errs()<<ANSI_COLOR(BG_WHITE,FG_GREEN)
        <<"indirect callsite, match"
        <<ANSI_COLOR_RESET<<"\n";
#endif
  for (auto *idc : idcs) {
#if 0
        errs()<<ANSI_COLOR_YELLOW<<" * ";
        idc->getDebugLoc().print(errs());
        errs()<<ANSI_COLOR_RESET<<"";
#endif
    // is this a trace point?
    // special condition, ignore tracepoint, we are not interested in them.
    if (is_tracepoint_func(idc->getCalledValue())) {
      count++;
      targets++;
      kmi_cnt++;
#if 0
            errs()<<" [tracepoint]\n";
#endif
      continue;
    }
    if (is_container_of(idc->getCalledValue())) {
      container_of_cnt++;
#if 0
            errs()<<" [container_of]\n";
#endif
      continue;
    }

    // try kmi
    // err - 0 no error
    //    - 1 undefined fptr in module, mark as resolved
    //    - 2 undefined module, mark as resolved(ok to fail)
    //    - 3 fptr comes from function parameter
    //    - 4 fptr comes from global fptr
    //    - 5 bad cast
    //    - 6 max error code- this is the bound
    int err = 6;
    // we resolved type and there's a matching object, but no fptr defined
    bool found_module = false;
    // we resolved type but there's no matching object
    bool udf_module = false;
    FunctionSet fs =
        resolve_indirect_callee_ldcst_kmi(idc, err, kmi_cnt, dkmi_cnt);
    if (err < 2)
      found_module = true;
    else if (err == 2)
      udf_module = true;

    if (fs.size() != 0) {
#if 0
            errs()<<" [LDCST-KMI]\n";
#endif
      goto resolved;
    }
    fs = resolve_indirect_callee_using_kmi(idc, err);
    if (err < 2)
      found_module = true;
    else if (err == 2)
      udf_module = true;

    if (fs.size() != 0) {
#if 0
            errs()<<" [KMI]\n";
#endif
      kmi_cnt++;
      goto resolved;
    }
    // using a fptr not implemented yet
    switch (err) {
    case (6):
    case (0): {
      goto unresolvable;
    }
    case (1):
    case (2): // try dkmi
    {
      // try dkmi
      break;
    }
    case (3): {
      // function parameter, unable to be solved by kmi and dkmi, try SVF
      fpar_cnt++;
      goto unresolvable;
    }
    case (4): {
      gptr_cnt++;
      goto unresolvable;
    }
    case (5): {
      cast_cnt++;
      goto unresolvable;
    }
    default:
      llvm_unreachable("no way!");
    }
    // try dkmi
    fs = resolve_indirect_callee_using_dkmi(idc, err);
    if (err < 2)
      found_module = true;
    else if (err == 2)
      udf_module = true;

    if (fs.size() != 0) {
#if 0
            errs()<<" [DKMI]\n";
#endif
      dkmi_cnt++;
      goto resolved;
    }
    if (found_module) {
#if 0
                errs()<<" [UNDEFINED1-found-m]\n";
#endif
      count++;
      targets++;
      undefined_1++;
      // dump_kmi_info(idc);
      continue;
    }
    if (udf_module) {
#if 0
            errs()<<" [UNDEFINED2-udf-m]\n";
#endif
      count++;
      targets++;
      undefined_2++;
      // dump_kmi_info(idc);
      continue;
    }
  unresolvable:
    // can not resolve
    fuidcs.insert(idc->getFunction());
    switch (err) {
    case (3): {
      // function parameter
#if 0
                errs()<<" [UPARA]\n";
#endif
      break;
    }
    case (4): {
      // global fptr
#if 0
                errs()<<" [GFPTR]\n";
#endif
      break;
    }
    case (5): {
#if 0
                errs()<<" [BAD CAST]\n";
#endif
      break;
    }
    default: {
#if 0
                errs()<<" [UNKNOWN]\n";
#endif
      unknown++;
      // dump the struct
      // dump_kmi_info(idc);
    }
    }
    continue;
  resolved:
    count++;
    targets += fs.size();
    FunctionSet *funcs = idcs2callee[idc];
    if (funcs == NULL) {
      funcs = new FunctionSet;
      idcs2callee[idc] = funcs;
    }
    for (auto f : fs) {
#if 0
            errs()<<"     - "<<f->getName()<<"\n";
#endif
      funcs->insert(f);
      InstructionSet *csis = f2csi_type1[f];
      if (csis == NULL) {
        csis = new InstructionSet;
        f2csi_type1[f] = csis;
      }
      csis->insert(idc);
    }
  }
  /*errs() << ANSI_COLOR(BG_WHITE, FG_RED) << "------ KMI STATISTICS ------"
         << ANSI_COLOR_RESET "\n";
  errs() << "# of indirect call sites: " << idcs.size() << "\n";
  errs() << "# resolved by KMI:" << count << " " << (100 * count / idcs.size())
         << "%\n";
  errs() << "#     - KMI:" << kmi_cnt << " " << (100 * kmi_cnt / idcs.size())
         << "%\n";
  errs() << "#     - DKMI:" << dkmi_cnt << " " << (100 * dkmi_cnt / idcs.size())
         << "%\n";
  errs() << "# (total target) of callee:" << targets << "\n";
  errs() << "# undefined-found-m : " << undefined_1 << " "
         << (100 * undefined_1 / idcs.size()) << "%\n";
  errs() << "# undefined-udf-m : " << undefined_2 << " "
         << (100 * undefined_2 / idcs.size()) << "%\n";
  errs() << "# fpara(KMI can not handle, try SVF?): " << fpar_cnt << " "
         << (100 * fpar_cnt / idcs.size()) << "%\n";
  errs() << "# global fptr(try SVF?): " << gptr_cnt << " "
         << (100 * gptr_cnt / idcs.size()) << "%\n";
  errs() << "# cast fptr(try SVF?): " << cast_cnt << " "
         << (100 * cast_cnt / idcs.size()) << "%\n";
  errs() << "# call use container_of(), high level type info stripped: "
         << container_of_cnt << " " << (100 * container_of_cnt / idcs.size())
         << "%\n";
  errs() << "# unknown pattern:" << unknown << " "
         << (100 * unknown / idcs.size()) << "%\n";*/
  // exit(0);
}

/*
 * method 2: cvf: Complex Value Flow Analysis
 * figure out candidate for indirect callee using value flow analysis
 */
void achyb::populate_indcall_list_using_cvf(Module &module) {
  // create svf instance
  CVFA cvfa;

  /*
   * NOTE: shrink our analyse scope so that we can run faster
   * remove all functions which don't have function pointer use and
   * function pointer propagation, because we only interested in getting
   * indirect callee here, this will help us make cvf run faster
   */
  FunctionSet keep;
  FunctionSet remove;
  // add skip functions to remove
  // add kernel_api to remove
  for (auto f : *skip_funcs)
    remove.insert(module.getFunction(f));
  for (auto f : *kernel_api)
    remove.insert(module.getFunction(f));
  for (auto f : trace_event_funcs)
    remove.insert(f);
  for (auto f : bpf_funcs)
    remove.insert(f);
  for (auto f : irq_funcs)
    remove.insert(f);

  FunctionList new_add;
  // for (auto f: all_functions)
  //    if (is_using_function_ptr(f) || is_address_taken(f))
  //        keep.insert(f);
  for (auto f : fuidcs)
    keep.insert(f);

  for (auto f : syscall_list)
    keep.insert(f);

  ModuleDuplicator md(module, keep, remove);
  Module &sm = md.getResult();

  // CVF: Initialize, this will take some time
  cvfa.initialize(sm);

  // do analysis(idcs=sink)
  // find out all possible value of indirect callee
  errs() << ANSI_COLOR(BG_WHITE, FG_BLUE)
         << "SVF indirect call track:" << ANSI_COLOR_RESET << "\n";
  for (auto f : all_functions) {
    ConstInstructionSet css;
    Function *df = dyn_cast<Function>(md.map_to_duplicated(f));
    cvfa.get_callee_function_indirect(df, css);
    if (css.size() == 0)
      continue;
    errs() << ANSI_COLOR(BG_CYAN, FG_WHITE) << "FUNC:" << f->getName()
           << ", found " << css.size() << ANSI_COLOR_RESET << "\n";
    for (auto *_ci : css) {
      // indirect call sites->function
      const CallInst *ci = dyn_cast<CallInst>(md.map_to_origin(_ci));
      assert(ci != NULL);
      FunctionSet *funcs = idcs2callee[ci];
      if (funcs == NULL) {
        funcs = new FunctionSet;
        idcs2callee[ci] = funcs;
      }
      funcs->insert(f);
      // func->indirect callsites
      InstructionSet *csis = f2csi_type1[f];
      if (csis == NULL) {
        csis = new InstructionSet;
        f2csi_type1[f] = csis;
      }
      CallInst *non_const_ci =
          const_cast<CallInst *>(static_cast<const CallInst *>(ci));

      csis->insert(non_const_ci);

#if 1
      errs() << "CallSite: ";
      ci->getDebugLoc().print(errs());
      errs() << "\n";
#endif
    }
  }
}

/*
 * need to populate idcs2callee before calling this function
 * should not call into this function using direct call
 */
FunctionSet achyb::resolve_indirect_callee(CallInst *ci) {
  FunctionSet fs;
  if (ci->isInlineAsm())
    return fs;
  if (get_callee_function_direct(ci))
    llvm_unreachable("resolved into direct call!");

  auto _fs = idcs2callee.find(ci);
  if (_fs != idcs2callee.end()) {
    for (auto *f : *(_fs->second))
      fs.insert(f);
  }

#if 0
    //FUZZY MATCHING
    //method 1: signature based matching
    //only allow precise match when collecting protected functions
        Value* cv = ci->getCalledValue();
        Type *ft = cv->getType()->getPointerElementType();
        if (!is_complex_type(ft))
            return fs;
        if (t2fs.find(ft)==t2fs.end())
            return fs;
        FunctionSet *fl = t2fs[ft];
        for (auto* f: *fl)
            fs.insert(f);
#endif
  return fs;
}
////////////////////////////////////////////////////////////////////////////////

/*
 * collect all gating function callsite
 * ----
 * f2chks: Function to Gating Function CallSite
 */
void achyb::collect_chkps(Module &module) {
  for (auto func : all_functions) {
    if (gating->is_gating_function(func))
      continue;

    InstructionSet *chks = f2chks[func];
    if (!chks) {
      chks = new InstructionSet();
      f2chks[func] = chks;
    }

    for (Function::iterator fi = func->begin(), fe = func->end(); fi != fe;
         ++fi) {
      BasicBlock *bb = dyn_cast<BasicBlock>(fi);
      for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie;
           ++ii) {
        if (CallInst *ci = dyn_cast<CallInst>(ii))
          if (Function *_f = get_callee_function_direct(ci)) {
            if (gating->is_gating_function(_f))
              chks->insert(ci);
          }
      }
    }
  }
#if 0
    //dump all checks
    for(auto& pair: f2chks)
    {
        ValueSet visited;
        Function* f = pair.first;
        InstructionSet* chkins = pair.second;
        if (chkins->size()==0)
            continue;
        gating->dump_interesting(chkins);
    }
#endif
}

/*
 * track user of functions which have checks, and see whether it is tied
 * to any interesting type(struct)
 */
Value *find_struct_use(Value *f, ValueSet &visited) {
  if (visited.count(f))
    return NULL;
  visited.insert(f);
  for (auto *u : f->users()) {
    if (u->getType()->isStructTy())
      return u;
    if (Value *_u = find_struct_use(u, visited))
      return _u;
  }
  return NULL;
}

void achyb::identify_interesting_struct(Module &module) {
  // first... functions which have checks in them
  for (auto &pair : f2chks) {
    ValueSet visited;
    Function *f = pair.first;
    InstructionSet *chkins = pair.second;
    if (chkins->size() == 0)
      continue;
    if (Value *u = find_struct_use(f, visited)) {
      StructType *type = dyn_cast<StructType>(u->getType());
      if (!type->hasName())
        continue;
      // should always skip this
      if (type->getStructName().startswith("struct.kernel_symbol"))
        continue;
      bool already_exists = is_interesting_type(type);
      /*errs() << "Function: " << f->getName() << " used by ";
      if (!already_exists)
        errs() << ANSI_COLOR_GREEN << " new discover:";
      if (type->getStructName().size() == 0)
        errs() << ANSI_COLOR_RED << "Annonymouse Type";
      else
        errs() << type->getStructName();
      errs() << ANSI_COLOR_RESET << "\n";*/
      discovered_interesting_type.insert(type);
    }
  }
#if 0
    //second... all functions
    for (auto f: all_functions)
    {
        ValueSet visited;
        if (Value* u = find_struct_use(f, visited))
        {
            StructType* type = dyn_cast<StructType>(u->getType());
            if (type->isLiteral())
                continue;
            if (!type->hasName())
                continue;
            if (type->getStructName().startswith("struct.kernel_symbol"))
                continue;
            bool already_exists = is_interesting_type(type);
            errs()<<"Function: "<<f->getName()
                <<" used by ";
            if (!already_exists)
                errs()<<ANSI_COLOR_GREEN<<" new discover:";
            if (type->getStructName().size()==0)
                errs()<<ANSI_COLOR_RED<<"Annonymouse Type";
            else
                errs()<<type->getStructName();
            errs()<<ANSI_COLOR_RESET<<"\n";
            discovered_interesting_type.insert(type);

        }
    }
#endif
  // sort functions
  for (auto f : all_functions) {
    StringRef fname = f->getName();
    if (fname.startswith("trace_event") || fname.startswith("perf_trace") ||
        fname.startswith("trace_raw")) {
      trace_event_funcs.insert(f);
      continue;
    }
    if (fname.startswith("bpf") || fname.startswith("__bpf") ||
        fname.startswith("___bpf")) {
      bpf_funcs.insert(f);
      continue;
    }
    if (fname.startswith("irq")) {
      irq_funcs.insert(f);
      continue;
    }

    ValueSet visited;
    Value *u = find_struct_use(f, visited);
    if (u) {
      bool skip = false;
      for (Value *v : visited)
        if (isa<Instruction>(v)) {
          assert("this is impossible\n");
          skip = true;
          break;
        }
      if (!skip)
        kmi_funcs.insert(f);
    }
  }
}

/*
 * this is used to identify any assignment of fptr to struct field, and we
 * collect this in complementary of identify_kmi
 */
void achyb::identify_dynamic_kmi(Module &module) {
  int cnt_resolved = 0;
  for (auto *f : all_functions) {
    Value *v = dyn_cast<Value>(f);
    Indices inds;
    ValueSet visited;
    StructType *t = find_assignment_to_struct_type(v, inds, visited);
    if (!t)
      continue;
    // Great! we got one! merge to know list or creat new

    cnt_resolved++;
    add_function_to_dmi(f, t, inds, dmi);
  }
  // errs() << "#dyn kmi resolved:" << cnt_resolved << "\n";
}

void achyb::dump_dkmi() {
  if (!knob_achyb_dkmi)
    return;
  errs() << ANSI_COLOR(BG_WHITE, FG_CYAN) << "=dynamic KMI=" << ANSI_COLOR_RESET
         << "\n";
  for (auto tp : dmi) {
    // type to metadata mapping
    StructType *t = tp.first;
    errs() << "Type:";
    if (t->isLiteral())
      errs() << "Literal\n";
    else
      errs() << t->getStructName() << "\n";
    // here comes the pairs
    IFPairs *ifps = tp.second;
    for (auto ifp : *ifps) {
      // indicies
      Indices *idcs = ifp->first;
      FunctionSet *fset = ifp->second;
      errs() << "  @ [";
      for (auto i : *idcs) {
        errs() << i << ",";
      }
      errs() << "]\n";
      // function names
      for (Function *f : *fset) {
        errs() << "        - ";
        errs() << f->getName();
        errs() << "\n";
      }
    }
  }
  errs() << "\n";
}

/*
 * identify logical kernel module
 * kernel module usually connect its functions to a struct that can be called
 * by upper layer
 * collect all global struct variable who have function pointer field
 */
void achyb::identify_kmi(Module &module) {
  // Module::GlobalListType &globals = module.getGlobalList();
  // not an interesting type, no function ptr inside this struct
  TypeSet nomo;
  for (GlobalVariable &gvi : module.globals()) {
    GlobalVariable *gi = &gvi;
    if (gi->isDeclaration())
      continue;
    assert(isa<Value>(gi));

    StringRef gvn = gi->getName();
    if (gvn.startswith("__kstrtab") || gvn.startswith("__tpstrtab") ||
        gvn.startswith(".str") || gvn.startswith("llvm.") ||
        gvn.startswith("__setup_str"))
      continue;

    Type *mod_interface = gi->getType();

    if (mod_interface->isPointerTy())
      mod_interface = mod_interface->getPointerElementType();
    if (!mod_interface->isAggregateType())
      continue;
    if (mod_interface->isArrayTy()) {
      mod_interface =
          dyn_cast<ArrayType>(mod_interface)->getTypeAtIndex((unsigned)0);
    }
    if (!mod_interface->isStructTy()) {
      if (mod_interface->isFirstClassType())
        continue;
      // report any non-first class type
      /* errs() << "IDKMI: aggregate type not struct?\n";
      mod_interface->print(errs());
      errs() << "\n";
      errs() << gi->getName() << "\n";*/
      continue;
    }
    if (nomo.find(mod_interface) != nomo.end())
      continue;
    // function pointer inside struct?
    if (!has_function_pointer_type(mod_interface)) {
      nomo.insert(mod_interface);
      continue;
    }
    // add
    ModuleSet *ms;
    if (mi2m.find(mod_interface) != mi2m.end()) {
      ms = mi2m[mod_interface];
    } else {
      ms = new ModuleSet;
      mi2m[mod_interface] = ms;
    }
    assert(ms);
    ms->insert(gi);
    // if (array_type)
    //    errs()<<"Added ArrayType:"<<gvn<<"\n";
  }
  TypeList to_remove;
  ModuleInterface2Modules to_add;
  // resolve Annoymous type into known type
  for (auto msi : mi2m) {
    StructType *stype = dyn_cast<StructType>(msi.first);
    if (stype->hasName())
      continue;
    StructType *rstype = NULL;
    assert(msi.second);
    for (auto m : (*msi.second)) {
      // constant bitcast into struct
      for (auto *_u : m->users()) {
        ConstantExpr *u = dyn_cast<ConstantExpr>(_u);
        BitCastInst *bciu = dyn_cast<BitCastInst>(_u);
        PointerType *type = NULL;
        if ((u) && (u->isCast())) {
          type = dyn_cast<PointerType>(u->getType());
          goto got_bitcast;
        }
        if (bciu) {
          type = dyn_cast<PointerType>(bciu->getType());
          goto got_bitcast;
        }
        // what else???
        continue;
      got_bitcast:
        // struct object casted into non pointer type?
        if (type == NULL)
          continue;
        StructType *_stype = dyn_cast<StructType>(type->getElementType());
        if ((!_stype) || (!_stype->hasName()))
          continue;
        rstype = _stype;
        goto out;
      }
    }
  out:
    if (!rstype)
      continue;
    // resolved, merge with existing type
    if (mi2m.find(rstype) != mi2m.end()) {
      ModuleSet *ms = mi2m[rstype];
      for (auto m : (*msi.second))
        ms->insert(m);
    } else if (to_add.find(rstype) != to_add.end()) {
      ModuleSet *ms = to_add[rstype];
      for (auto m : (*msi.second))
        ms->insert(m);
    } else {
      // does not exists? reuse current one!
      to_add[rstype] = msi.second;
      /*
       * this should not cause crash as we already parsed current element
       * and this should be set to NULL in order to not be deleted later
       */
      mi2m[stype] = NULL;
    }
    to_remove.push_back(stype);
  }
  for (auto r : to_remove) {
    delete mi2m[r];
    mi2m.erase(r);
  }
  for (auto r : to_add)
    mi2m[r.first] = r.second;
}

/*
 * populate cache
 * --------------
 * all_functions
 * t2fs(Type to FunctionSet)
 * syscall_list
 * f2csi_type0 (Function to BitCast CallSite)
 * idcs(indirect call site)
 */
void achyb::preprocess(Module &module) {
  initialize_achyb_sets(knob_skip_func_list, knob_skip_var_list,
                         knob_crit_symbol, knob_kernel_api);

  for (Module::iterator fi = module.begin(), f_end = module.end(); fi != f_end;
       ++fi) {
    Function *func = dyn_cast<Function>(fi);
    if (func->isDeclaration()) {
      ExternalFuncCounter++;
      continue;
    }
    if (func->isIntrinsic())
      continue;

    FuncCounter++;

    all_functions.insert(func);
    Type *type = func->getFunctionType();
    FunctionSet *fl = t2fs[type];
    if (fl == NULL) {
      fl = new FunctionSet;
      t2fs[type] = fl;
    }
    fl->insert(func);

    if (is_syscall_prefix(func->getName()))
      syscall_list.insert(func);

    for (Function::iterator fi = func->begin(), fe = func->end(); fi != fe;
         ++fi) {
      BasicBlock *bb = dyn_cast<BasicBlock>(fi);
      for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie;
           ++ii) {
        CallInst *ci = dyn_cast<CallInst>(ii);
        if (!ci || ci->getCalledFunction() || ci->isInlineAsm())
          continue;

        Value *cv = ci->getCalledValue();
        Function *bcf = dyn_cast<Function>(cv->stripPointerCasts());
        if (bcf) {
          // this is actually a direct call with function type cast
          InstructionSet *csis = f2csi_type0[bcf];
          if (csis == NULL) {
            csis = new InstructionSet;
            f2csi_type0[bcf] = csis;
          }
          csis->insert(ci);
          continue;
        }
        idcs.insert(ci);
      }
    }
  }

}

/*
 * collect critical resources
 */
void achyb::collect_crits(Module &module) {
  for (auto pair : f2chks) {
    Function *func = pair.first;
    InstructionSet *_chks = pair.second;
    if ((_chks == NULL) || (_chks->size() == 0) ||
        gating->is_gating_function(func) || is_skip_function(func->getName())) {
      continue;
    }
    dbgstk.push_back(func->getEntryBlock().getFirstNonPHI());
    InstructionList callgraph;
    InstructionList chks;
    forward_all_interesting_usage(func->getEntryBlock().getFirstNonPHI(), 0,
                                  false, callgraph, chks);
    dbgstk.pop_back();
  }

  CRITFUNC = critical_functions.size();
  CRITVAR = critical_variables.size();
  CritFuncSkip = skipped_functions.size();
  errs() << "Critical functions skipped because of skip func list: "
         << CritFuncSkip << "\n";
}

void achyb::collect_achyb_priv_funcs(Module &module) {
  for (Module::iterator mi = module.begin(); mi != module.end(); ++mi) {
    Function *f = dyn_cast<Function>(mi);
    if(!f || is_skip_function(f->getName()) || gating->is_gating_function(f)) {
      continue;
    }
    if(f->getName().find("lock") != StringRef::npos) {
      continue;
    }

    /*if(f->getName().find("random_write") != StringRef::npos) {
      errs() << "Special: " << f->getName() << "\n";
    }

    if(f->getName().find("_extract_crng") != StringRef::npos) {
      errs() << "Special: " << f->getName() << "\n";
    }*/

    /*if(f->getName().find("random_write") != StringRef::npos) {
      errs() << "Special: " << f->getName() << "\n";
    }*/

    // debug
    CallInstSet guard_cis = get_guard_callsites(f);

    // Yang: debug
    if(guard_cis.size() == 0) {
      continue;
    }

    for (Function::iterator fi = f->begin(), fe = f->end(); fi != fe; ++fi) {
      BasicBlock *bb = dyn_cast<BasicBlock>(fi);
      for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie; ++ii) {
        CallInst *ci = dyn_cast<CallInst>(ii);
        if(!ci) {
          continue;
        }

        Function* f_target = get_callee_function_direct(ci);
        if (!f_target) {
          continue;
        }

        if(gating->is_gating_function(f_target)) {
          CallInstSet cis = dependence_analysis(ci);
          
          // errs() << "callee\n";
          for(auto tmp_ci : cis) {
            Function* f_callee = get_callee_function_direct(tmp_ci); // curr_ci->getCalledFunction();
            if (f_callee) {
              // direct call
              if(f_callee->getName().find("llvm.") == StringRef::npos && !is_skip_function(f_callee->getName())
                && f_callee->getName().find("ioctl") == StringRef::npos && f_callee->getName().find("lock") == StringRef::npos) {
                critical_functions.insert(f_callee);
                critical_evidences[f_callee] = tmp_ci;
              }
            } else {
              FunctionSet fs = resolve_indirect_callee(tmp_ci);

              for(auto f_indirect_callee : fs) {
                if(f_indirect_callee->getName().find("llvm.") == StringRef::npos && !is_skip_function(f_indirect_callee->getName())
                  && f_indirect_callee->getName().find("ioctl") == StringRef::npos && f_indirect_callee->getName().find("lock") == StringRef::npos) {
                  critical_functions.insert(f_indirect_callee);
                  critical_evidences[f_indirect_callee] = tmp_ci;
                }
              }
              
            }
            // FunctionSet fs = get_call_tree(ci);
            
          }
          // errs() << "callee ended\n";
        }
      }
    }
    // errs() << f->getName() << " ended\n";
  }

  // Debug
  for(auto f : critical_functions) {
    errs() << f->getName() << "\n";
  }

  errs() << "priv_func_num="<< critical_functions.size() << "\n";

}

CallInstSet achyb::dependence_analysis(CallInst* ci) {
  //errs() << "DEBUG:\n" << *ci << "\n";
  auto f = ci->getParent()->getParent();

  std::unordered_set<BranchInst*> branches;

  InstructionList queue;
  InstructionSet visited;
  queue.push_back(ci);
  while(queue.size() > 0) {
    auto inst = *queue.begin();
    queue.pop_front();

    for(auto user : inst->users()) {
      auto curr_inst = dyn_cast<Instruction>(user);
      if (curr_inst) {
        if(visited.find(curr_inst) == visited.end()) {
          // Debug
          // errs() << *curr_inst << "\n";

          visited.insert(curr_inst);
          auto curr_branch_inst = dyn_cast<BranchInst>(curr_inst);
          if(curr_branch_inst) {
            branches.insert(curr_branch_inst);
          }
          queue.push_back(curr_inst);
        }
      }
    }
  }

  // Yang: blocl-level dominate analysis
	CallInstSet cis;
	BasicBlockSet dominated;
	for(auto b_inst: branches) {
		std::vector<BasicBlockSet> dominated_lst;
		for(auto bb : b_inst->successors()) {
      auto branch_block = bb->getUniquePredecessor();
			if(branch_block) { // bb->getUniquePredecessor() != nullptr
				BasicBlockSet one_dominated;
  
				one_dominated.insert(bb);

				bool is_loop = true;
				while(is_loop) {
					is_loop = false;
					for (Function::iterator fi = f->begin(), fe = f->end(); fi != fe; ++fi) {
						BasicBlock *bb = dyn_cast<BasicBlock>(fi);
						if(one_dominated.find(bb) == one_dominated.end()) {
							bool is_dominated = true;
							bool is_empty = true;
							for(auto pred_bb : predecessors(bb)) {
								is_empty = false;
								if(one_dominated.find(pred_bb) == one_dominated.end()) {
									is_dominated = false;
									break;
								}
							}

							if(is_empty) {
								is_dominated = false;
							}

							if(is_dominated) {
							    one_dominated.insert(bb);
							    is_loop = true;
							    break;
							}
						} 
					}
				}
      
				dominated_lst.push_back(one_dominated);
			}
		}

		

		for(int i = 0; i < dominated_lst.size(); i++) {
			auto& one_dominated = dominated_lst[i];
			for(auto bb : one_dominated) {
				bool is_repeated = false;
				for(int j = 0; j < dominated_lst.size(); j++) {
					if(i == j) {
						continue;
					}

					auto& two_dominated = dominated_lst[j];
					if(two_dominated.find(bb) != two_dominated.end()) {
						is_repeated = true;
						break;
					}
				}

				if(!is_repeated) {
					dominated.insert(bb);
				}

			}
		}

		// dominated.insert(one_dominated.begin(), one_dominated.end());
	}

  for(auto bb : dominated) {
    for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie; ++ii) {
      CallInst *ci = dyn_cast<CallInst>(ii);
      if(!ci) {
        continue;
      }
      cis.insert(ci);
    }
  }
  

  return cis;
}

CallInstSet achyb::strict_dependence_analysis(CallInst* ci) {
  //errs() << "DEBUG:\n" << *ci << "\n";
	std::unordered_set<BranchInst*> branches;

	InstructionList queue;
  InstructionSet visited;
  queue.push_back(ci);
  while(queue.size() > 0) {
    auto inst = *queue.begin();
    queue.pop_front();

    for(auto user : inst->users()) {
      auto curr_inst = dyn_cast<Instruction>(user);
      if (curr_inst) {
        if(visited.find(curr_inst) == visited.end()) {
          visited.insert(curr_inst);
          auto curr_branch_inst = dyn_cast<BranchInst>(curr_inst);
          if(curr_branch_inst) {
            branches.insert(curr_branch_inst);
          }
          queue.push_back(curr_inst);
        }
      }
    }
  }

  CallInstSet cis;
  BasicBlockSet dominated;
  
  for(auto b_inst: branches) {
		std::vector<BasicBlockSet> dominated_lst;
		for(auto bb : b_inst->successors()) {
			if(bb->getUniquePredecessor()) {
				BasicBlockSet one_dominated;
				one_dominated.insert(bb);

				bool is_loop = true;
				auto f = ci->getParent()->getParent();
				while(is_loop) {
					is_loop = false;
					for (Function::iterator fi = f->begin(), fe = f->end(); fi != fe; ++fi) {
						BasicBlock *bb = dyn_cast<BasicBlock>(fi);
						if(one_dominated.find(bb) == one_dominated.end()) {
							bool is_dominated = true;
							bool is_empty = true;
							for(auto pred_bb : predecessors(bb)) {
							  is_empty = false;
							  if(one_dominated.find(pred_bb) == one_dominated.end()) {
							    is_dominated = false;
							    break;
							  }
							}

							if(is_empty) {
							  is_dominated = false;
							}

							if(is_dominated) {
							    one_dominated.insert(bb);
							    is_loop = true;
							    break;
							}
						} 
					}
				}
				dominated_lst.push_back(one_dominated);
			}
		}

		for(int i = 0; i < dominated_lst.size(); i++) {
			auto& one_dominated = dominated_lst[i];
			for(auto bb : one_dominated) {
				bool is_repeated = false;
				for(int j = 0; j < dominated_lst.size(); j++) {
					if(i == j) {
						continue;
					}

					auto& two_dominated = dominated_lst[j];
					if(two_dominated.find(bb) != two_dominated.end()) {
						is_repeated = true;
						break;
					}
				}

				if(!is_repeated) {
					dominated.insert(bb);
				}

			}
		}

		// dominated.insert(one_dominated.begin(), one_dominated.end());
	}

	for(auto bb : dominated) {
		for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie; ++ii) {
		  CallInst *ci = dyn_cast<CallInst>(ii);
		  if(!ci) {
		    continue;
		  }
		  cis.insert(ci);
		}
	}
	return cis;
}

FunctionSet achyb::get_call_tree(CallInst* ci) {
  // debug
  // errs() << "call tree starts\n";
  errs() << ci->getName() << "\n";

  FunctionSet fs;
  
  InstructionSet visited;
  // visited.insert(ci);

  CallInstList call_queue;
  call_queue.push_back(ci);
  while(call_queue.size() > 0) {
    // debug
    errs() << "call_queue_size=" << call_queue.size() << "\n";

    CallInst* curr_ci = *call_queue.begin();
    call_queue.pop_front();
    visited.insert(curr_ci);
    
    FunctionSet fs;

    //errs() << curr_ci->getName() << "\n";
    //errs() << "call direct resolve\n";
    Function* f = get_callee_function_direct(curr_ci); // curr_ci->getCalledFunction();
    //errs() << "call direct resolve ended\n";
    if (f) {
      // direct call
      if(!is_skip_function(f->getName())) {
        fs.insert(f);
      }

    } else {
      // indirect call
      //errs() << "call indirect resolve\n";
      fs = resolve_indirect_callee(curr_ci);
      //errs() << "call indirect resolve ended\n";

    }
    //errs() << "fs_size=" << fs.size() << "\n";
    // debug
    errs() << "fs_size=" << fs.size() << "\n";
    
    for(auto target_f : fs) {
      for (Function::iterator fi = target_f->begin(), fe = target_f->end(); fi != fe; ++fi) {
        BasicBlock *bb = dyn_cast<BasicBlock>(fi);
        for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie; ++ii) {
          CallInst* next_ci = dyn_cast<CallInst>(ii);
          if (!next_ci) {
            continue;
          }

          if(visited.find(next_ci) == visited.end()) {
            call_queue.push_back(next_ci);
          }
        }
      }
    }
    
  }
  return fs;
}

/*
 * discover checks inside functions f, including checks inside other callee
 * this is shared across all workers.
 */
pthread_rwlock_t dc_lock;

InstructionSet *achyb::discover_chks(Function *f, FunctionSet &visited) {
  InstructionSet *ret = NULL;
  if (visited.count(f)) {
    pthread_rwlock_rdlock(&dc_lock);
    if (f2chks_disc.count(f))
      ret = f2chks_disc[f];
    pthread_rwlock_unlock(&dc_lock);
    return ret;
  }
  visited.insert(f);

  ret = new InstructionSet;

  // any direct check
  if (InstructionSet *chks = f2chks[f])
    for (auto *i : *chks)
      ret->insert(i);

  // indirect check
  for (Function::iterator fi = f->begin(), fe = f->end(); fi != fe; ++fi) {
    BasicBlock *bb = dyn_cast<BasicBlock>(fi);
    for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie;
         ++ii) {
      CallInst *ci = dyn_cast<CallInst>(ii);
      if (!ci)
        continue;
      if (ci->isInlineAsm())
        continue;

      Function *nextf = get_callee_function_direct(ci);
      if (!nextf) // ignore all indirect call
        continue;
      if (InstructionSet *r = discover_chks(nextf, visited))
        if (r->size())
          ret->insert(ci);
    }
  }
  pthread_rwlock_wrlock(&dc_lock);
  if (f2chks_disc.count(f) == 0) {
    f2chks_disc[f] = ret;
  } else {
    delete ret;
    ret = f2chks_disc[f];
  }
  pthread_rwlock_unlock(&dc_lock);
  return ret;
}

InstructionSet *achyb::discover_chks(Function *f) {
  pthread_rwlock_rdlock(&dc_lock);
  if (f2chks_disc.count(f) != 0) {
    InstructionSet *ret = f2chks_disc[f];
    pthread_rwlock_unlock(&dc_lock);
    return ret;
  }
  pthread_rwlock_unlock(&dc_lock);

  FunctionSet visited;
  InstructionSet *ret = discover_chks(f, visited);
  return ret;
}

void achyb::backward_slice_build_callgraph(InstructionList &callgraph,
                                            Instruction *I,
                                            FunctionToCheckResult &fvisited,
                                            int &good, int &bad, int &ignored) {
  // I should be an instruction
  if (!I)
    return;
  // we've reached the limit
  if (callgraph.size() > knob_bwd_depth) {
    BwdAnalysisMaxHit++;
    return;
  }
  Function *f = I->getFunction();
  if (fvisited.find(f) != fvisited.end()) {
    switch (fvisited[f]) {
    case (RCHKED):
      good++;
      break;
    case (RNOCHK):
      bad++;
      break;
    case (RNA):
      break;
    default:
      ignored++;
      break;
    }
    return;
  }
  callgraph.push_back(I);
  // place holder
  fvisited[f] = RNA;
  DominatorTree dt(*f);
  InstructionSet *chks;

  if (is_skip_function(f->getName())) {
    // should skip?
    ignored++;
    dump_as_ignored(callgraph);
    goto ignored_out;
  }
 
  chks = discover_chks(f);
  if ((chks != NULL) && chks->size()) {
    for (auto *chk : *chks) {
      if (dt.dominates(chk, I)) {
        if (knob_dump_good_path) {
          errs() << ANSI_COLOR(BG_GREEN, FG_BLACK)
                 << "Hit Check Function:" << get_callee_function_name(chk)
                 << " @ ";
          chk->getDebugLoc().print(errs());
          errs() << ANSI_COLOR_RESET << "\n";
        }
        good++;
        goto good_out;
      }
    }
  }

  if (is_syscall(f)) {
    // this is syscall and no check, report it as bad
    bad++;
    goto bad_out;
  }
 
  // Direct CallSite
  for (auto *U : f->users()) {
    CallInstSet cis;
    get_callsite_inst(U, cis);
    bool resolved_as_call = false;
    for (auto *_ci : cis) {
      if (_ci->getCalledFunction() != f) {
        // this should match otherwise it is used as a call back function
        //(function parameter)which is not our interest
        // errs()<<"Function "<<f->getName()<< " used as a callback @ ";
        //_ci->getDebugLoc().print(errs());
        // errs()<<"\n";
        ignored++;
        dump_as_ignored(callgraph);
        continue;
      }
      resolved_as_call = true;
      backward_slice_build_callgraph(callgraph, dyn_cast<Instruction>(U),
                                     fvisited, good, bad, ignored);
    }
    if (!resolved_as_call) {
      if (!isa<Instruction>(U)) {
        CFuncUsedByStaticAssign++;
        // must be non-instruction
        if (is_used_by_static_assign_to_interesting_type(U)) {
          // used as kernel entry point and no check
          bad++;
          dump_as_bad(callgraph);
        } else {
          ignored++;
          dump_as_ignored(callgraph);
        }
      } else {
        // other use of current function?
        // llvm_unreachable("what?");
        CFuncUsedByNonCallInst++;
      }
    }
  }
  // Indirect CallSite(also user of current function)
  backward_slice_using_indcs(f, callgraph, fvisited, good, bad, ignored);

// intermediate.. just return.
ignored_out:
  callgraph.pop_back();
  return;

good_out:
  fvisited[f] = RCHKED;
  dump_as_good(callgraph);
  callgraph.pop_back();
  return;

bad_out:
  fvisited[f] = RNOCHK;
  dump_as_bad(callgraph);
  callgraph.pop_back();
  return;
}

void achyb::_backward_slice_reachable_to_chk_function(Instruction *I,
                                                       int &good, int &bad,
                                                       int &ignored) {
  InstructionList callgraph;
  // FIXME: should consider function+instruction pair as visited?
  FunctionToCheckResult fvisited;
  return backward_slice_build_callgraph(callgraph, I, fvisited, good, bad,
                                        ignored);
}

void achyb::backward_slice_reachable_to_chk_function(Instruction *cs,
                                                      int &good, int &bad,
                                                      int &ignored) {
  // collect all path and meet condition
  _backward_slice_reachable_to_chk_function(cs, good, bad, ignored);
}

/*
 * exact match with bitcast
 */
bool achyb::match_cs_using_fptr_method_0(Function *func,
                                          InstructionList &callgraph,
                                          FunctionToCheckResult &visited,
                                          int &good, int &bad, int &ignored) {
  bool ret = false;
  InstructionSet *csis;

  if (f2csi_type0.find(func) == f2csi_type0.end())
    goto end;
  csis = f2csi_type0[func];

  ret = true;
  for (auto *csi : *csis)
    backward_slice_build_callgraph(callgraph, csi, visited, good, bad, ignored);
end:
  return ret;
}

/*
 * signature based method to find out indirect callee
 */
bool achyb::match_cs_using_fptr_method_1(Function *func,
                                          InstructionList &callgraph,
                                          FunctionToCheckResult &visited,
                                          int &good, int &bad, int &ignored) {
  // we want exact match to non-trivial function
  int cnt = 0;
  Type *func_type = func->getFunctionType();
  FunctionSet *fl = t2fs[func_type];
  if (!is_complex_type(func_type))
    goto end;
  if ((fl == NULL) || (fl->size() != 1))
    goto end;
  if ((*fl->begin()) != func)
    goto end;
  for (auto *idc : idcs) {
    Value *cv = idc->getCalledValue();
    Type *ft = cv->getType()->getPointerElementType();
    Type *ft2 = cv->stripPointerCasts()->getType()->getPointerElementType();
    if ((func_type == ft) || (func_type == ft2)) {
      cnt++;
      // errs()<<"Found matched functions for indirectcall:"
      //    <<(*fl->begin())->getName()<<"\n";
      backward_slice_build_callgraph(callgraph, idc, visited, good, bad,
                                     ignored);
    }
  }
end:
  return cnt != 0;
}

/*
 * get result from value flow analysis result
 */
bool achyb::match_cs_using_cvf(Function *func, InstructionList &callgraph,
                                FunctionToCheckResult &visited, int &good,
                                int &bad, int &ignored) {
  // TODO: optimize this
  int cnt = 0;
  for (auto *idc : idcs) {
    auto fs = idcs2callee.find(idc);
    if (fs == idcs2callee.end())
      continue;
    for (auto *f : *(fs->second)) {
      if (f != func)
        continue;
      cnt++;
      backward_slice_build_callgraph(callgraph, idc, visited, good, bad,
                                     ignored);
      break;
    }
  }
  return cnt != 0;
}

bool achyb::backward_slice_using_indcs(Function *func,
                                        InstructionList &callgraph,
                                        FunctionToCheckResult &visited,
                                        int &good, int &bad, int &ignored) {
  bool ret;
  /*
   * direct call using bitcast
   * this is exact match don't need to look further
   */
  ret = match_cs_using_fptr_method_0(func, callgraph, visited, good, bad,
                                     ignored);
  if (ret)
    return ret;

  if (!knob_achyb_cvf) {
    ret = match_cs_using_fptr_method_1(func, callgraph, visited, good, bad,
                                       ignored);
    if (ret)
      MatchCallCriticalFuncPtr++;
    else
      UnMatchCallCriticalFuncPtr++;
    return ret;
  }
  ret = match_cs_using_cvf(func, callgraph, visited, good, bad, ignored);
  if (ret)
    MatchCallCriticalFuncPtr++;
  else
    UnMatchCallCriticalFuncPtr++;
  return ret;
}

/*
 * check possible critical function path
 *
 * tid - thread id
 * wgsize - total number of threads
 */
void achyb::check_critical_function_usage(Module &module) {
  witidx = 0;
  if (knob_mt <= 1) {
    _check_critical_function_usage(&module, 0, 1);
    return;
  }
  pthread_rwlock_init(&dc_lock, NULL);

  std::thread workers[(int)knob_mt];
  for (unsigned int i = 0; i < knob_mt; i++)
    workers[i] = std::thread(&achyb::_check_critical_function_usage, this,
                             &module, i, (int)knob_mt);
  for (unsigned int i = 0; i < knob_mt; i++)
    workers[i].join();
  dump_statistics();
}

void achyb::_check_critical_function_usage(Module *module, int tid,
                                            int wgsize) {
  /*
   * collect critical indirect call site and check them in one shot
   */
  InstructionSet indirect_callsite_set;
  /*
   * for each critical function find out all callsite(use)
   */
  int idx = 0;
  for (Function *func : critical_functions) {
    if (idx < witidx) {
      idx++;
      continue;
    }
    witidx_lock.lock();
    if (idx == witidx) // revalidate
    {
      // success
      idx++;
      witidx = idx;
    } else {
      // failed
      idx++;
      witidx_lock.unlock();
      continue;
    }

    // Yang: print progress
	  // errs() << "Yang Progress:" << idx << " / " << critical_functions.size() << "\n";

    witidx_lock.unlock();

    if (!crit_syms->use_builtin())             // means that not knob specified
      if (!crit_syms->exists(func->getName())) // means that symbol not matched
        continue;
    if (is_skip_function(func->getName()))
      continue;

    // x_lock.lock();
    errs() << ANSI_COLOR_YELLOW << "Check Use of Function:" << func->getName()
           << ANSI_COLOR_RESET << "\n";
    // x_lock.unlock();
    // iterate through all call site
    // direct call
    int good = 0, bad = 0, ignored = 0;
    for (auto *U : func->users()) {
      CallInstSet cil;
      get_callsite_inst(U, cil);
      for (auto cs : cil)
        backward_slice_reachable_to_chk_function(cs, good, bad, ignored);
    }
    // indirect call
#if 0
        for (auto& callees: idcs2callee)
        {
            CallInst *cs = const_cast<CallInst*>
                            (static_cast<const CallInst*>(callees.first));
            for (auto* f: *callees.second)
                if (f==func)
                {
                    indirect_callsite_set.insert(cs);
                    break;
                }
        }
#else
    if (f2csi_type1.find(func) != f2csi_type1.end())
      for (auto cs : *f2csi_type1[func])
        indirect_callsite_set.insert(cs);
#endif
    // summary
    if (bad != 0) {
      errs() << ANSI_COLOR_GREEN << "Good: " << good << " " << ANSI_COLOR_RED
             << "Bad: " << bad << " " << ANSI_COLOR_YELLOW
             << "Ignored: " << ignored << ANSI_COLOR_RESET << "\n";
    }
    BadPath += bad;
    GoodPath += good;
    IgnPath += ignored;
  }
#if 1
  // critical indirect call site
  errs() << ANSI_COLOR_YELLOW << "Check all other indirect call sites"
         << ANSI_COLOR_RESET << "\n";
  int good = 0, bad = 0, ignored = 0;
  for (auto cs : indirect_callsite_set) {
    errs() << ANSI_COLOR_YELLOW << "Check callee group:" << ANSI_COLOR_RESET
           << "\n";
    for (auto func : *idcs2callee[cs]) {
      if (!crit_syms->use_builtin()) // means that not knob specified
        if (!crit_syms->exists(func->getName())) // means that symbol not
                                                 // matched
          continue;
      if (is_skip_function(func->getName()) ||
          (critical_functions.find(func) == critical_functions.end()))
        continue;
      errs() << "    " << func->getName() << "\n";
    }
    backward_slice_reachable_to_chk_function(cs, good, bad, ignored);
  }
  // summary
  if (bad != 0) {
    errs() << ANSI_COLOR_GREEN << "Good: " << good << " " << ANSI_COLOR_RED
           << "Bad: " << bad << " " << ANSI_COLOR_YELLOW
           << "Ignored: " << ignored << ANSI_COLOR_RESET << "\n";
  }
  BadPath += bad;
  GoodPath += good;
  IgnPath += ignored;
#endif
  x_lock.lock();
  errs() << ANSI_COLOR(BG_WHITE, FG_GREEN) << "Thread " << tid << " Done!"
         << ANSI_COLOR_RESET << "\n";
  x_lock.unlock();
}

void achyb::check_critical_variable_usage(Module &module) {
  witidx = 0;
  if (knob_mt == 1) {
    _check_critical_variable_usage(&module, 0, 1);
    return;
  }
  std::thread *workers;
  workers = new std::thread[knob_mt];
  for (unsigned int i = 0; i < knob_mt; i++)
    workers[i] = std::thread(&achyb::_check_critical_variable_usage, this,
                             &module, i, (int)knob_mt);
  for (unsigned int i = 0; i < knob_mt; i++)
    workers[i].join();
  delete workers;
}
void achyb::_check_critical_variable_usage(Module *module, int tid,
                                            int wgsize) {
  int idx = 0;
  for (auto *V : critical_variables) {
    if (idx < witidx) {
      idx++;
      continue;
    }
    witidx_lock.lock();
    if (idx == witidx) // revalidate
    {
      // success
      idx++;
      witidx = idx;
    } else {
      // failed
      idx++;
      witidx_lock.unlock();
      continue;
    }
    witidx_lock.unlock();

    FunctionList flist; // known functions
    errs() << ANSI_COLOR_YELLOW << "Inspect Use of Variable:" << V->getName()
           << ANSI_COLOR_RESET << "\n";

    // figure out all use-def, put them info workset
    InstructionSet workset;
    for (auto *U : V->users()) {
      Instruction *ui = dyn_cast<Instruction>(U);
      if (!ui) // not an instruction????
      {
        // llvm_unreachable("not an instruction?");
        continue;
      }
      Function *f = ui->getFunction();
      // make sure this is not inside a kernel init function
      if (is_kernel_init_functions(f))
        continue;
      // TODO: value flow
      workset.insert(ui);
    }
    for (auto *U : workset) {
      Function *f = U->getFunction();
      errs() << " @ " << f->getName() << " ";
      U->getDebugLoc().print(errs());
      errs() << "\n";
      flist.push_back(f);

      // is this instruction reachable from non-checked path?
      int good = 0, bad = 0, ignored = 0;
      _backward_slice_reachable_to_chk_function(dyn_cast<Instruction>(U), good,
                                                bad, ignored);
      if (bad != 0) {
        errs() << ANSI_COLOR_GREEN << "Good: " << good << " " << ANSI_COLOR_RED
               << "Bad: " << bad << " " << ANSI_COLOR_YELLOW
               << "Ignored: " << ignored << ANSI_COLOR_RESET << "\n";
      }
      BadPath += bad;
      GoodPath += good;
      IgnPath += ignored;
    }
  }
}

void achyb::check_critical_type_field_usage(Module &module) {
  witidx = 0;
  if (knob_mt == 1) {
    _check_critical_type_field_usage(&module, 0, 1);
    return;
  }
  std::thread *workers;
  workers = new std::thread[knob_mt];
  for (unsigned int i = 0; i < knob_mt; i++)
    workers[i] = std::thread(&achyb::_check_critical_type_field_usage, this,
                             &module, i, (int)knob_mt);
  for (unsigned int i = 0; i < knob_mt; i++)
    workers[i].join();
  delete workers;
}

void achyb::_check_critical_type_field_usage(Module *module, int tid,
                                              int wgsize) {
  int idx = 0;

  for (auto V : critical_typefields) {
    if (idx < witidx) {
      idx++;
      continue;
    }
    witidx_lock.lock();
    if (idx == witidx) // revalidate
    {
      // success
      idx++;
      witidx = idx;
    } else {
      // failed
      idx++;
      witidx_lock.unlock();
      continue;
    }
    witidx_lock.unlock();

    StructType *t = dyn_cast<StructType>(V.first);
    // std::set<int>& fields = V.second;

    errs() << ANSI_COLOR_YELLOW << "Inspect Use of Type:" << t->getStructName()
           << ANSI_COLOR_RESET << "\n";

    // figure out all use-def, put them info workset
    InstructionSet workset;
    // figure out where the type is used, and add all of them in workset
    // mainly gep
    figure_out_gep_using_type_field(workset, V, *module);

    for (auto *U : workset) {
      Function *f = U->getFunction();
      errs() << " @ " << f->getName() << " ";
      U->getDebugLoc().print(errs());
      errs() << "\n";

      // is this instruction reachable from non-checked path?
      int good = 0, bad = 0, ignored = 0;
      _backward_slice_reachable_to_chk_function(dyn_cast<Instruction>(U), good,
                                                bad, ignored);
      if (bad != 0) {
        errs() << ANSI_COLOR_GREEN << "Good: " << good << " " << ANSI_COLOR_RED
               << "Bad: " << bad << " " << ANSI_COLOR_YELLOW
               << "Ignored: " << ignored << ANSI_COLOR_RESET << "\n";
      }
      BadPath += bad;
      GoodPath += good;
      IgnPath += ignored;
    }
  }
}

void achyb::figure_out_gep_using_type_field(
    InstructionSet &workset,
    const std::pair<Type *, std::unordered_set<int>> &v, Module &module) {
  for (Module::iterator f = module.begin(), f_end = module.end(); f != f_end;
       ++f) {
    if (is_skip_function(dyn_cast<Function>(f)->getName()))
      continue;
    for (Function::iterator fi = f->begin(), fe = f->end(); fi != fe; ++fi) {
      BasicBlock *bb = dyn_cast<BasicBlock>(fi);
      for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie;
           ++ii) {
        if (!isa<GetElementPtrInst>(ii))
          continue;
        GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(ii);
        /*if (!gep_used_by_call_or_store(gep))
            continue;*/
        Type *gep_operand_type =
            dyn_cast<PointerType>(gep->getPointerOperandType())
                ->getElementType();
        // check type
        if (gep_operand_type == v.first) {
          // check field
          assert(gep->hasIndices());
          ConstantInt *cint = dyn_cast<ConstantInt>(gep->idx_begin());
          if (!cint)
            continue;
          int idx = cint->getSExtValue();
          if (v.second.count(idx))
            workset.insert(gep);
        }
      }
    }
  }
}
/*
 * collect critical function calls,
 * callee of direct call is collected directly,
 * callee of indirect call is reasoned by its type or struct
 */
void achyb::crit_func_collect(CallInst *cs, FunctionSet &current_crit_funcs,
                               InstructionList &chks) {
  // ignore inline asm
  if (cs->isInlineAsm())
    return;
  if (Function *csf = get_callee_function_direct(cs)) {
    if (csf->isIntrinsic() || is_skip_function(csf->getName()) ||
        gating->is_gating_function(csf)) {
      if (is_skip_function(csf->getName()))
        skipped_functions.insert(csf);
      return;
    }
    current_crit_funcs.insert(csf);

    if (knob_achyb_ccfv) {
      errs() << "Add call<direct> " << csf->getName() << " use @ ";
      cs->getDebugLoc().print(errs());
      errs() << "\n cause:";
      dump_dbgstk(dbgstk);
    }

    InstructionSet *ill = f2ci[csf];
    if (ill == NULL) {
      ill = new InstructionSet;
      f2ci[csf] = ill;
    }
    for (auto chki : chks)
      ill->insert(chki);

  } // else if (Value* csv = cs->getCalledValue())
  else if (cs->getCalledValue() != NULL) {
    if (knob_achyb_ccfv) {
      errs() << "Resolve indirect call @ ";
      cs->getDebugLoc().print(errs());
      errs() << "\n";
    }
    // TODO:solve as gep function pointer of struct
    FunctionSet fs = resolve_indirect_callee(cs);
    if (!fs.size()) {
      if (knob_achyb_ccfv)
        errs() << ANSI_COLOR_RED << "[NO MATCH]" << ANSI_COLOR_RESET << "\n";
      CPUnResolv++;
      return;
    }
    if (knob_achyb_ccfv)
      errs() << ANSI_COLOR_GREEN << "[FOUND " << fs.size() << " MATCH]"
             << ANSI_COLOR_RESET << "\n";
    CPResolv++;
    for (auto *csf : fs) {
      if (csf->isIntrinsic() || is_skip_function(csf->getName()) ||
          gating->is_gating_function(csf) || (csf == cs->getFunction())) {
        if (is_skip_function(csf->getName()))
          skipped_functions.insert(csf);
        continue;
      }

      current_crit_funcs.insert(csf);
      if (knob_achyb_ccfv) {
        errs() << "Add call<indirect> " << csf->getName() << " use @ ";
        cs->getDebugLoc().print(errs());
        errs() << "\n cause:";
        dump_dbgstk(dbgstk);
      }
      InstructionSet *ill = f2ci[csf];
      if (ill == NULL) {
        ill = new InstructionSet;
        f2ci[csf] = ill;
      }
      // insert all chks?
      for (auto chki : chks)
        ill->insert(chki);
    }
  }
}


void achyb::crit_vars_collect(Instruction *ii,
                               ValueList &current_critical_variables,
                               InstructionList &chks) {
  Value *gv = get_global_def(ii);
  if (gv && (!isa<Function>(gv)) && (!is_skip_var(gv->getName()))) {
    if (knob_achyb_ccvv) {
      errs() << "Add " << gv->getName() << " use @ ";
      ii->getDebugLoc().print(errs());
      errs() << "\n cause:";
      dump_dbgstk(dbgstk);
    }
    current_critical_variables.push_back(gv);
    InstructionSet *ill = v2ci[gv];
    if (ill == NULL) {
      ill = new InstructionSet;
      v2ci[gv] = ill;
    }
    for (auto chki : chks)
      ill->insert(chki);
  }
}


void achyb::crit_type_field_collect(Instruction *i,
                                     Type2Fields &current_t2fmaps,
                                     InstructionList &chks) {
  StructType *t = NULL;
  if (LoadInst *li = dyn_cast<LoadInst>(i)) {
    if (!li->getType()->isPointerTy())
      return;
    Value *addr = li->getPointerOperand()->stripPointerCasts();
    // now we are expecting a gep
    if (GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(addr)) {
      // great we got a gep
      // Value* gep_operand = gep->getPointerOperand();
      Type *gep_operand_type =
          dyn_cast<PointerType>(gep->getPointerOperandType())->getElementType();
      if (!isa<StructType>(gep_operand_type))
        return;
      // FIXME: only handle the first field as of now
      assert(gep->hasIndices());
      if (!(dyn_cast<ConstantInt>(gep->idx_begin()))) {
        return;
      }
      // what is the first indice?
      StructType *stype = dyn_cast<StructType>(gep_operand_type);
      if (is_skip_struct(stype->getStructName()))
        return;
      int idx = dyn_cast<ConstantInt>(gep->idx_begin())->getSExtValue();
      current_t2fmaps[gep_operand_type].insert(idx);
      t = stype;
      goto goodret;
    } else {
      // what else? maybe phi?
    }
  } else if (StoreInst *si = dyn_cast<StoreInst>(i)) {
    if (!si->getValueOperand()->getType()->isPointerTy())
      return;
    Value *addr = si->getPointerOperand()->stripPointerCasts();
    if (GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(addr)) {
      // great we got a gep
      // Value* gep_operand = gep->getPointerOperand();
      Type *gep_operand_type =
          dyn_cast<PointerType>(gep->getPointerOperandType())->getElementType();
      if (!isa<StructType>(gep_operand_type))
        return;
      // FIXME: only handle the first field as of now
      assert(gep->hasIndices());
      if (!(dyn_cast<ConstantInt>(gep->idx_begin()))) {
        return;
      }
      StructType *stype = dyn_cast<StructType>(gep_operand_type);
      if (is_skip_struct(stype->getStructName()))
        return;

      int idx = dyn_cast<ConstantInt>(gep->idx_begin())->getSExtValue();
      current_t2fmaps[gep_operand_type].insert(idx);
      t = stype;
      goto goodret;
    } else {
      // TODO
    }
  } else {
    // TODO
  }
  return;
goodret:
  InstructionSet *ill = t2ci[t];
  if (ill == NULL) {
    ill = new InstructionSet;
    t2ci[t] = ill;
  }
  for (auto i : chks)
    ill->insert(i);
  if (knob_achyb_cctv) {
    errs() << "Add struct " << t->getStructName() << " use @ ";
    i->getDebugLoc().print(errs());
    errs() << "\n cause:";
    dump_dbgstk(dbgstk);
  }

  return;
}


void achyb::forward_all_interesting_usage(Instruction *I, unsigned int depth,
                                           bool checked,
                                           InstructionList &callgraph,
                                           InstructionList &chks) {
  Function *func = I->getFunction();
  DominatorTree dt(*func);

  if (is_skip_function(func->getName())) {
    skipped_functions.insert(func);
    return;
  }

  bool is_function_permission_checked = checked;
  /*
   * collect interesting variables/functions found in this function
   */
  FunctionSet current_crit_funcs;
  ValueList current_critical_variables;
  Type2Fields current_critical_type_fields;

  // don't allow recursive
  if (std::find(callgraph.begin(), callgraph.end(), I) != callgraph.end())
    return;

  callgraph.push_back(I);

  if (depth > knob_fwd_depth) {
    callgraph.pop_back();
    FwdAnalysisMaxHit++;
    return;
  }

  BasicBlockSet bb_visited;
  BasicBlockList bb_work_list;

  /*
   * a list of instruction where check functions are used,
   * that will be later used to do dominance checking
   */
  InstructionList chk_instruction_list;

  /*****************************
   * first figure out all checks
   */
  // already checked?
  if (is_function_permission_checked)
    goto rescan_and_add_all;

  bb_work_list.push_back(I->getParent());
  while (bb_work_list.size()) {
    BasicBlock *bb = bb_work_list.front();
    bb_work_list.pop_front();
    if (bb_visited.count(bb))
      continue;
    bb_visited.insert(bb);

    for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie;
         ++ii) {
      if (isa<CallInst>(ii)) {
        // only interested in call site
        CallInst *ci = dyn_cast<CallInst>(ii);
        if (Function *csfunc = get_callee_function_direct(ci)) {
          if (gating->is_gating_function(csfunc)) {
            is_function_permission_checked = true;
            chk_instruction_list.push_back(ci);
            chks.push_back(ci);
          }
        } else if (!ci->isInlineAsm()) {
          // don't really care inline asm
          // FIXME:this is in-direct call, could there be a check inside
          // indirect call we are missing?
        }
      }
    }
    // insert all successor of current basic block to work list
    for (succ_iterator si = succ_begin(bb), se = succ_end(bb); si != se; ++si)
      bb_work_list.push_back(cast<BasicBlock>(*si));
  }

  if (!is_function_permission_checked)
    goto out;

/*******************************************************************
 * second, re-scan all instructions and figure out
 * which one can be dominated by those check instructions(protected)
 */
rescan_and_add_all:

  for (Function::iterator fi = func->begin(), fe = func->end(); fi != fe;
       ++fi) {
    BasicBlock *bb = dyn_cast<BasicBlock>(fi);
    for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie;
         ++ii) {
      Instruction *si = dyn_cast<Instruction>(ii);
      /*
       * if any check dominate si then go ahead and
       * add them to protected list
       */
      // already checked before entering current scope
      // all following usage should be dominated by incoming Instruction
      if (checked) {
        // should dominate use
        if (dt.dominates(I, si))
          goto add;
      }
      // or should have newly discovered check.. and
      // there should be at least one check dominate the use
      for (auto *_ci : chk_instruction_list)
        if (dt.dominates(_ci, si))
          goto add;
      // dont care if not protected
      continue;

    add:
      if (CallInst *cs = dyn_cast<CallInst>(ii)) {
        crit_func_collect(cs, current_crit_funcs, chks);
        // continue;
        // need to look at argument
      }
      crit_vars_collect(si, current_critical_variables, chks);
      crit_type_field_collect(si, current_critical_type_fields, chks);
    }
  }
  // still not checked???
  if (!is_function_permission_checked)
    goto out;
  /*
   * checked, merge forward slicing result(intra-) and collect more(inter-)
   */
  for (auto i : current_crit_funcs)
    critical_functions.insert(i);
  for (auto v : current_critical_variables)
    critical_variables.insert(v);
  for (auto v : current_critical_type_fields) {
    Type *t = v.first;
    std::unordered_set<int> &sset = v.second;
    std::unordered_set<int> &dset = critical_typefields[t];
    for (auto x : sset)
      dset.insert(x);
  }
  // FIXME: handle indirect callsite
  for (auto *U : func->users()) {
    if (CallInst *cs = dyn_cast<CallInst>(U)) {
      Function *pfunc = cs->getFunction();
      if (pfunc->isIntrinsic())
        continue;
      if (is_kernel_init_functions(pfunc)) {
        if (knob_warn_achyb_during_kinit) {
          dbgstk.push_back(cs);
          errs() << ANSI_COLOR_YELLOW
                 << "capability check used during kernel initialization\n"
                 << ANSI_COLOR_RESET;
          dump_dbgstk(dbgstk);
          dbgstk.pop_back();
        }
        continue;
      }

      dbgstk.push_back(cs);
      forward_all_interesting_usage(cs, depth + 1, true, callgraph, chks);
      dbgstk.pop_back();
    }
  }
out:
  callgraph.pop_back();
  return;
}


void achyb::achyb_process(Module &module) {
  // errs() << "Preprocess:\n";
  // errs() << "Done\n\n";

  errs() << "Permission Checks:\n";
  STOP_WATCH_START(WID_0);
  preprocess(module);
  if(knob_achyb_semiauto) {
    gating = new PermCheck(module, knob_pclist_path, true);
  } else {
    gating = new PermCheck(module, knob_pclist_path, false);
  }
  STOP_WATCH_STOP(WID_0);
  STOP_WATCH_REPORT(WID_0);
  errs() << "Done\n\n";

  errs() << "Privileged Function Detection:\n";
  STOP_WATCH_START(WID_0);
  collect_chkps(module);
  identify_interesting_struct(module);
  collect_kernel_init_functions(module);
  identify_kmi(module);
  // dump_kmi();
  identify_dynamic_kmi(module);
  // dump_dkmi();
  populate_indcall_list_through_kmi(module);
  collect_achyb_priv_funcs(module);
  STOP_WATCH_STOP(WID_0);
  STOP_WATCH_REPORT(WID_0);
  errs() << "Done\n\n";
  
  if(knob_achyb_pexinv) {
    errs() << "PeX Invariant Analysis:\n";
    STOP_WATCH_MON(WID_0, check_critical_function_usage(module));

    errs() << "Done\n\n";
  } else {
    errs() << "Constraint-based Analysis:\n";
    STOP_WATCH_MON(WID_0, constraint_analysis(module));
    errs() << "Done\n\n";
  }

  delete gating;
  // exit(0);
}

CallInstSet achyb::get_guard_callsites(Function* f) {
	CallInstSet guard_cis;
	for (Function::iterator fi = f->begin(), fe = f->end(); fi != fe; ++fi) {
      BasicBlock *bb = dyn_cast<BasicBlock>(fi);
      for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie; ++ii) {
        CallInst *ci = dyn_cast<CallInst>(ii);
        if(!ci) {
          continue;
        }

        Function* f_target = get_callee_function_direct(ci);
        if (!f_target) {
          continue;
        }

        if(gating->is_gating_function(f_target)) {
          // Debug
          // errs() << "dependence\n";
          CallInstSet cis = strict_dependence_analysis(ci);
          guard_cis.insert(cis.begin(), cis.end());
        }
      }
    }

    return guard_cis;
}

CallInstSet achyb::get_caller_callsites(Module &module, Function* f_def) {
	CallInstSet cis;
  if(caller_map.find(f_def) != caller_map.end()) {
    auto cis_lst = caller_map[f_def];
    cis.insert(cis_lst.begin(), cis_lst.end());
    return cis;
  } 

	for (Module::iterator mi = module.begin(); mi != module.end(); ++mi) {
	    Function *f = dyn_cast<Function>(mi);
	    if(!f || f == f_def) {
	    	continue;
	    }

	    for (Function::iterator fi = f->begin(), fe = f->end(); fi != fe; ++fi) {
      		BasicBlock *bb = dyn_cast<BasicBlock>(fi);
      		for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie; ++ii) {
		        CallInst *ci = dyn_cast<CallInst>(ii);
		        if(!ci) {
		        	continue;
		        }

		        Function* f_callee = get_callee_function_direct(ci); // curr_ci->getCalledFunction();
        		if (f_callee) {
        			if(f_callee == f_def) {
        				cis.insert(ci);
        			}
		    	} else {
            // debug
             //errs() << "indirect\n";

		          FunctionSet fs_indirect = resolve_indirect_callee(ci);

		          if(fs_indirect.find(f_def) != fs_indirect.end()) {
		          	cis.insert(ci);
		          }
		        }
		    }
      }
	}

  
  if(caller_map.find(f_def) == caller_map.end()) {
    std::list<CallInst*> cis_lst;

    for(auto ci : cis) {
      cis_lst.push_back(ci);
    }
    
    caller_map[f_def] = cis_lst;
  } 
  
	return cis;
}

void achyb::constraint_analysis(Module &module) {
  std::unordered_map<Function*, int> ps;
  std::unordered_map<Function*, CallInst*> pv;

  for (Module::iterator mi = module.begin(); mi != module.end(); ++mi) {
    Function *f = dyn_cast<Function>(mi);
    if(!f || is_skip_function(f->getName()) || gating->is_gating_function(f) || critical_functions.find(f) != critical_functions.end()) {
      continue;
    }

    /*if(!f || gating->is_gating_function(f)) {
      continue;
    }*/


    // errs() << f->getName() << "\n";
    CallInstSet guard_cis = get_guard_callsites(f);

    // Yang: debug
    /*if(guard_cis.size() == 0) {
      continue;
    }*/
    
    for (Function::iterator fi = f->begin(), fe = f->end(); fi != fe; ++fi) {
      BasicBlock *bb = dyn_cast<BasicBlock>(fi);
      for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie; ++ii) {
        CallInst *ci = dyn_cast<CallInst>(ii);
        if(!ci) {
        	continue;
        }

        if(guard_cis.find(ci) != guard_cis.end()) {
        	continue;
        }

        Function* f_callee = get_callee_function_direct(ci); // curr_ci->getCalledFunction();
        if (f_callee) {
          // direct call
          if(f_callee->getName().find("llvm.") == StringRef::npos && 
          	// !is_skip_function(f_callee->getName()) &&
          	critical_functions.find(f_callee) != critical_functions.end()) {

          	if(ps.find(f_callee) == ps.end()) {
          		ps[f_callee] = 0;

          	}

          	ps[f_callee] += 1;
          	pv[f_callee] = ci;
          }
        } else {
          FunctionSet fs = resolve_indirect_callee(ci);

          for(auto f_indirect_callee : fs) {
            if(f_indirect_callee->getName().find("llvm.") == StringRef::npos && 
            	!is_skip_function(f_indirect_callee->getName()) &&
            	critical_functions.find(f_indirect_callee) != critical_functions.end()) {

            	if(ps.find(f_indirect_callee) == ps.end()) {
	          		ps[f_indirect_callee] = 0;
	          	}
	          	ps[f_indirect_callee] += 1;
	          	pv[f_indirect_callee] = ci;
            }
          }  
        }
      }
    }
    // errs() << f->getName() << " ended\n";
  }

  
  
  std::unordered_map<Function*, Function*> report;
  for(auto pair : ps) {
  	auto f = pair.first;
  	auto cnt = pair.second;
  	if(cnt >= 1) { // Yang: cnt == 1
  		auto ci = pv[f];
  		auto buggy_func = ci->getParent()->getParent();
  		if(report.find(buggy_func) == report.end()) {

        // Yang: TODO: check if their callers are protected
        FunctionSet func_visited;
        bool is_caller_protected = true;
        std::list<Function*> upf_queue;
        upf_queue.push_back(buggy_func);

        int t = 0;
        while(upf_queue.size() > 0) {
          auto curr_upf = *upf_queue.begin();
          upf_queue.pop_front();

          auto caller_cis = get_caller_callsites(module, curr_upf);

          if(caller_cis.size() == 0) {

            if(curr_upf->getName().find("__se_sys_") != StringRef::npos || 
              curr_upf->getName().find("__se_compat_") != StringRef::npos) {
              is_caller_protected = false;
              // errs() << "call begin: " << curr_upf->getName() << "\n";
              // break;
            }

            // is_caller_protected = false;
            // errs() << "call begin: " << curr_upf->getName() << "\n";
            break;
          }

          if(t > 2) {
            is_caller_protected = false;
            break;
          }
          t += 1;

          CallInstSet upc_set;
          for(auto caller_ci : caller_cis) {
            auto caller_func = caller_ci->getParent()->getParent();
            auto caller_guard_cis = get_guard_callsites(caller_func);
            if(caller_guard_cis.find(caller_ci) == caller_guard_cis.end()) {
              upc_set.insert(caller_ci);
              break;
            }
          }

          for(auto upc : upc_set) {
            auto ucf = upc->getParent()->getParent();
            if(func_visited.find(ucf) != func_visited.end()) {
              continue;
            }

            func_visited.insert(curr_upf);
            upf_queue.push_back(ucf);
          }
        }


  			if(!is_caller_protected) {
  				report[buggy_func] = f;
  			}
        //report[buggy_func] = f;
  		}

  	}
  }

  // errs() << "Results:\n";
  for(auto pair : report) {
  	auto buggy_func = pair.first;
  	auto f = pair.second;

  	auto evidence_ci = critical_evidences[f];
  	errs() << buggy_func->getName() << ":" << f->getName() << ":" << evidence_ci->getParent()->getParent()->getName() << "\n";
  }

  errs() << "total_cnt=" << report.size() << "\n";
}


bool achyb::runOnModule(Module &module) {
  m = &module;
  return achybPass(module);
}

bool achyb::achybPass(Module &module) {
  /*errs() << ANSI_COLOR_CYAN << "--- PROCESS FUNCTIONS ---" << ANSI_COLOR_RESET
         << "\n";*/

  achyb_process(module);
  // process_cpgf(module);

  /*errs() << ANSI_COLOR_CYAN << "--- DONE! ---" << ANSI_COLOR_RESET << "\n";*/

#if CUSTOM_STATISTICS
  // dump_statistics();
#endif
  // just quit
  exit(0);
  // never reach here
  // Yang
  return true; // return true if the module was modified by the transformation and false otherwise.
}

static RegisterPass<achyb>
    XXX("achyb", "achyb");
