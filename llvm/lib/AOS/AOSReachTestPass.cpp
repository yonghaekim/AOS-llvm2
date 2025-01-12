#include "llvm/AOS/AOSReachTestPass.h"

#define PARTS_USE_SHA3

extern "C" {
#include "../PARTS-sha3/include/sha3.h"
}

char AOSReachTestPass::ID = 0;
static RegisterPass<AOSReachTestPass> X("aos-reach", "AOS reacheability test pass");

Pass *llvm::AOS::createAOSReachTestPass() { return new AOSReachTestPass(); }

bool AOSReachTestPass::runOnModule(Module &M) {
	bool function_modified = false;

	//Baseline = true;
	//AOS = true;
	//WYFY_C = true;
	//WYFY_F = true;
	WYFY_FT = true;

	errs() << "Start reachability test pass!\n";

  AOSMarkTaintPass &MT = getAnalysis<AOSMarkTaintPass>();
  value_map = MT.getValueMap();

	init(M);

	//handleCmdLineArguments(M);
	if (!Baseline) {
  if (!AOS) {
  function_modified = handleGlobalVariables(M) || function_modified;
  }
  function_modified = handleInstructions(M) || function_modified;

	function_modified = handlePtrToInts(M) || function_modified;
	}
	errs() << "statNumGV: " << statNumGV << "\n";
	errs() << "statNumGVSigned: " << statNumGVSigned << "\n";
	errs() << "statNumAI: " << statNumAI << "\n";
	errs() << "statNumAISigned: " << statNumAISigned << "\n";
	errs() << "statNumCI: " << statNumCI << "\n";
	errs() << "statNumCISigned: " << statNumCISigned << "\n";

  //// Insert wyfy_print_func() to count # of functions called
  //// only for called functions?!
	//int func_num = 0;
	//for (auto &F : M) {
  //  bool chk = false;
	//	if (&F && !F.isDeclaration()) {
  //    for (auto &BB : F) {
  //      for (auto &I : BB) {
  //        if (dyn_cast<ReturnInst>(&I)) {
  //          Module *pM = F.getParent();
  //          IRBuilder<> Builder(&I);

  //          auto bbc = Intrinsic::getDeclaration(F.getParent(), Intrinsic::wyfy_bbc);
  //          Builder.CreateCall(bbc, {}, "");
  //          chk = true;
  //          break;
  //          //Value *arg = ConstantInt::get(Type::getInt64Ty(pM->getContext()), func_num++);
  //          //FunctionType *FuncTypeA = FunctionType::get(Type::getVoidTy(pM->getContext()), {Type::getInt64Ty(pM->getContext())}, false);
  //          //Constant *promote = F.getParent()->getOrInsertFunction("wyfy_print_func", FuncTypeA);
  //          //auto callA = Builder.CreateCall(promote, {arg});
  //        }
  //      }

  //      if (chk)
  //        break;
  //    }
  //  }
  //}

  return function_modified;
}

void AOSReachTestPass::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.setPreservesAll();
  AU.addRequired<AOSMarkTaintPass> ();
}

//void AOSReachTestPass::handleCmdLineArguments(Module &M) {
//  for (auto &F : M) {
//		if (&F && F.getName() == "main") {
//			unsigned t = 0;
//
//			Value *arg0, *arg1;
//
//			for (auto arg = F.arg_begin(); arg != F.arg_end(); arg++) {
//				// argc
//				if (t == 0) {
//					arg0 = arg;
//				// argv
//				} else if (t == 1) {
//					arg1 = arg;
//
//			auto &BB = F.front();
//			auto &I = BB.front();
//
//			IRBuilder<> Builder(&I);
//			FunctionType *FuncTypeA = FunctionType::get(Type::getVoidTy(*C), {Type::getInt32Ty(*C), PointerType::get(Type::getInt8PtrTy(*C),
//																										dyn_cast<PointerType>(arg1->getType())->getAddressSpace()), Type::getInt64Ty(*C)}, false);
//			auto typeIdConstant = getTypeIDConstantFrom(*(arg1->getType()), *C);
//			Constant *promote = M.getOrInsertFunction("_Z18wyfy_promote_argvsiPPcm", FuncTypeA);
//			auto callA = Builder.CreateCall(promote, {arg0, arg1, typeIdConstant});
//
//			//_Z18wyfy_promote_argvsiPPcm
//
//
//
//
////          //AOSNode *node = value_map[arg];
////					//printNode(node);
////					list<AOSNode *> node_list;
////					set<AOSNode *> visit_set;
////
////					for (auto it = value_map[arg]->children.begin(); it != value_map[arg]->children.end(); it++) {
////						if (visit_set.find(*it) == visit_set.end()) {
////							visit_set.insert(*it);
////							node_list.push_back(*it);
////						}
////					}
////
////					while (!node_list.empty()) {
////						AOSNode *node = node_list.front();
////						node_list.pop_front();
////
////						errs() << "Print Aliases! isMemNode: " << (node->mem_user ? "True" : "False") << "\n";
////						printNode(node);
////
////						for (auto it = node->children.begin(); it != node->children.end(); it++) {
////							if (visit_set.find(*it) == visit_set.end()) {
////								visit_set.insert(*it);
////								node_list.push_back(*it);
////							}
////						}
////
////						errs() << "--------------------------\n";
////					}
//				} else {
//					break;
//				}
//
//				t++;
//			}
//		}
//	}
//}

bool AOSReachTestPass::handleGlobalVariables(Module &M) {
	bool function_modified = false;

	for (auto &G : M.getGlobalList()) {
		GlobalVariable *pGV = dyn_cast<GlobalVariable>(&G);
		Type *ty = pGV->getType()->getElementType();

		// Skip constant GV
		if (pGV->isConstant() && pGV->getName().find(".str") == 0)
			continue;

		if (pGV->use_empty() || pGV->getLinkage() == GlobalValue::LinkOnceODRLinkage)
			continue;

    if (!pGV->hasInitializer() || pGV->isDeclaration())
      continue;

		//if (ty->isArrayTy() || ty->isStructTy())
		if (!ty->isArrayTy() && !IsStructTyWithArray(ty))
			continue;

		statNumGV++;

		if (!WYFY_FT || doReachabilityTest(value_map[pGV])) {
			gv_list.push_back(pGV);
			statNumGVSigned++;
		}
  }

	for (auto &F : M) {
		if (&F && F.getName() == "main") {
			while (!gv_list.empty()) {
				GlobalVariable *pGV = dyn_cast<GlobalVariable>(gv_list.front());
				gv_list.pop_front();
				Type *ty = pGV->getType()->getElementType();

				//assert(ty->isArrayTy() || ty->isStructTy());

				handleGlobalVariable(&F, pGV);
			}

			break;
		}
	}
	
	return function_modified;
}

bool AOSReachTestPass::handleGlobalVariable(Function *pF, GlobalVariable *pGV) {
	set<Instruction *> user_set;
	set<GlobalVariable *> gv_set;

	for (auto pU: pGV->users()) {
		if (auto *pI = dyn_cast<Instruction>(pU))
			user_set.insert(pI);
		else if (auto *pGV = dyn_cast<GlobalVariable>(pU))
			gv_set.insert(pGV);
	}

	map<Function *, map<Value *, Value *>> func_map;
	Type *ty = pGV->getType()->getElementType();
	auto size = DL->getTypeSizeInBits(ty);
	Value *arg = ConstantInt::get(Type::getInt64Ty(*C), size / 8);

	set<Type *> type_set;
	type_set = getStructTypes(ty, type_set);

	//errs() << "getStructTypes of: ";
	//ty->dump();
	//for (auto x: type_set)
	//	x->dump();
	//errs() << "-------------------\n";

  std::vector<Type *> arg_type;
  Type *retType = Type::getInt8PtrTy(*C);
  arg_type.push_back(retType);

	auto &BBF = pF->front(); // main
	auto &IF = BBF.front();
  IRBuilder<> BuilderF(&IF);
  auto castA = BuilderF.CreateCast(Instruction::BitCast, pGV, Type::getInt8PtrTy(*C));
	if (WYFY_C || WYFY_F || WYFY_FT) {
		auto typeIdConstant = getTypeIDConstantFrom(*pGV->getType(), *C);
		auto pacma = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_pacma_ty, arg_type);
		auto bndstr = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_bndstr, arg_type);
		auto callA = BuilderF.CreateCall(pacma, {castA, arg, typeIdConstant}, ""); 
		auto callB = BuilderF.CreateCall(bndstr, {callA, arg}, "");
		auto castB = BuilderF.CreateCast(Instruction::BitCast, callB, pGV->getType());
		func_map[pF][pGV] = castB;
	} else {
		auto castB = BuilderF.CreateCast(Instruction::BitCast, castA, pGV->getType());
		func_map[pF][pGV] = castB;
	}

	for (auto pI: user_set) {
		auto _pF = pI->getFunction();
		auto &BB = _pF->front();
		auto &I = BB.front();
		
		Value *cur_val = func_map[_pF][pGV];

		if (cur_val == nullptr) {
			IRBuilder<> Builder(&I);
			auto castA = Builder.CreateCast(Instruction::BitCast, pGV, Type::getInt8PtrTy(*C));
			if (WYFY_C || WYFY_F || WYFY_FT) {
				auto typeIdConstant = getTypeIDConstantFrom(*pGV->getType(), *C);
				auto pacma = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_pacma_ty, arg_type);
				auto callA = Builder.CreateCall(pacma, {castA, arg, typeIdConstant}, ""); 
				auto castB = Builder.CreateCast(Instruction::BitCast, callA, pGV->getType());

				cur_val = castB;
				func_map[_pF][pGV] = castB;
			} else {
				auto castB = Builder.CreateCast(Instruction::BitCast, castA, pGV->getType());

				cur_val = castB;
				func_map[_pF][pGV] = castB;
			}
		}

		unsigned cnt = 0;
		bool chk = false;
		for (auto it = pI->op_begin(); it != pI->op_end(); it++) {
      if (dyn_cast<LandingPadInst>(pI))
        continue;

			if ((*it) == pGV) {
				pI->setOperand(cnt, cur_val);
				chk = true;
				break;
			}
				
			cnt++;
		}

		assert(chk);
	}

	//// Handle global pointer pointing to global variable
	//for (auto _pGV: gv_set) {
	//	if (_pGV->getInitializer() == pGV) {
	//		pGV->dump();
	//		auto &BB = pF->front();
	//		auto &I = BB.front();
	//	
	//		Value *cur_val = func_map[pF][pGV];

	//		if (cur_val == nullptr) {
	//			IRBuilder<> Builder(&I);
	//			auto castA = Builder.CreateCast(Instruction::BitCast, pGV, Type::getInt8PtrTy(*C));
	//			if (WYFY_C || WYFY_F || WYFY_FT) {
	//				auto typeIdConstant = getTypeIDConstantFrom(*pGV->getType(), *C);
	//				auto pacma = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_pacma_ty, arg_type);
	//				auto bndstr = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_bndstr, arg_type);
	//				auto callA = Builder.CreateCall(pacma, {castA, arg, typeIdConstant}, ""); 
	//				auto callB = Builder.CreateCall(bndstr, {callA, arg}, "");
	//				auto castB = Builder.CreateCast(Instruction::BitCast, callB, pGV->getType());

	//				cur_val = castB;
	//				func_map[pF][pGV] = castB;
	//			} else {
	//				auto castB = Builder.CreateCast(Instruction::BitCast, castA, pGV->getType());

	//				cur_val = castB;
	//				func_map[pF][pGV] = castB;
	//			}
	//		}

	//		IRBuilder<> Builder(dyn_cast<Instruction>(cur_val)->getNextNode());
	//		Builder.CreateStore(cur_val, _pGV);
	//	}
	//}

	//if (IsStructTy(ty))
	if ((WYFY_F || WYFY_FT) && IsStructTyWithArray(ty))
		handleStruct(pF, pGV, type_set);

	return true;
}

void AOSReachTestPass::replaceUsesInFunction(Value *pV, Value *pNew, Function *pF) {
	for (auto it = pV->use_begin(); it != pV->use_end(); it++) {
		Use &U = *it;
		User *pU = U.getUser();

		errs() << "pU->dump(): "; pU->dump();
		if (auto *pI = dyn_cast<Instruction>(pU)) {
			if (pI->getFunction() == pF)
				U.set(pNew);
		}
	}
}

bool AOSReachTestPass::handleInstructions(Module &M) {
  bool function_modified = false;

	for (auto &F : M) {
		for (auto &BB : F) {
			for (auto &I : BB) {
        switch (I.getOpcode()) {
          case Instruction::Alloca:
          {
            AllocaInst *pAI = dyn_cast<AllocaInst>(&I);
						Type *ty = pAI->getAllocatedType();
						Function *pF = pAI->getFunction();

						if (ty->isArrayTy() || IsStructTyWithArray(ty)) {
								statNumAI++;

							if (!WYFY_FT || doReachabilityTest(value_map[pAI])) {
								statNumAISigned++;
                inst_list.push_back(pAI);
							}
						}

            break;
          }
          case Instruction::Call:
          case Instruction::Invoke:
          {
						Function *pF = nullptr;

						if (CallInst *pCI = dyn_cast<CallInst>(&I))
            	pF = pCI->getCalledFunction();
						else if (InvokeInst *pII = dyn_cast<InvokeInst>(&I))
            	pF = pII->getCalledFunction();

            if (pF && (pF->getName() == "malloc" ||
                        pF->getName() == "_Znwm" /* new */ ||
                        pF->getName() == "_Znam" /* new[] */ ||
                        pF->getName() == "calloc" ||
                        pF->getName() == "realloc")) {
							statNumCI++;

							if (!WYFY_FT || doReachabilityTest(value_map[&I])) {
								statNumCISigned++;
                inst_list.push_back(&I);
							}
            } else if (pF && (pF->getName() == "free" ||
															pF->getName() == "_ZdlPv" ||
															pF->getName() == "_ZdaPv")) {

							//if (!WYFY_FT || doReachabilityTest(value_map[pCI->getOperand(0)]))
                inst_list.push_back(&I);
            }

            break;
          }
          default:
            break;
        }
      }
    }
  }

	while (!inst_list.empty()) {
		Instruction *pI = inst_list.front();
		Function *pF = pI->getFunction();
		inst_list.pop_front();

		if (AllocaInst *pAI = dyn_cast<AllocaInst>(pI)) {
			Type *ty = pAI->getAllocatedType();

			function_modified = handleAlloca(pF, pAI) || function_modified;
		}

		CallInst *pCI = dyn_cast<CallInst>(pI);
		InvokeInst *pII = dyn_cast<InvokeInst>(pI);

		if (pCI || pII) {
			Function *called_func = nullptr;
			if (pCI)
				called_func = pCI->getCalledFunction();
			else if (pII)
				called_func = pII->getCalledFunction();

			if (called_func && (called_func->getName() == "malloc" ||
                        called_func->getName() == "_Znwm" /* new */ ||
                        called_func->getName() == "_Znam" /* new[] */ ||
												called_func->getName() == "calloc" ||
												called_func->getName() == "realloc")) {

				function_modified = handleMalloc(pF, pI) || function_modified;
			} else if (called_func && (called_func->getName() == "free" ||
												called_func->getName() == "_ZdlPv" ||
												called_func->getName() == "_ZdaPv")) {

				function_modified = handleFree(pF, pI) || function_modified;
			} else {
				pCI->dump();
				assert(false);
			}
		}
	}

	return function_modified;
}

bool AOSReachTestPass::handlePtrToInts(Module &M) {
	if (Baseline)
		return false; 

	bool function_modified = false;

  std::vector<Type *> arg_type;
  Type *retType = Type::getInt8PtrTy(*C);
  arg_type.push_back(retType);

	for (auto &F : M) {
		for (auto &BB : F) {
			for (auto &I : BB) {
				if (auto pPTI = dyn_cast<PtrToIntInst>(&I)) {
					//pPTI->dump();
					auto ptr = pPTI->getPointerOperand();

					IRBuilder<> Builder(&I);
					auto castA = Builder.CreateCast(Instruction::BitCast, ptr, Type::getInt8PtrTy(*C));
					auto xpacm = Intrinsic::getDeclaration(F.getParent(), Intrinsic::wyfy_xpacm, arg_type);
				  auto callA = Builder.CreateCall(xpacm, {castA}, "");
					auto castB = Builder.CreateCast(Instruction::BitCast, callA, ptr->getType());

					pPTI->setOperand(0, castB);
					function_modified = true;
				} else if (auto pCI = dyn_cast<ICmpInst>(&I)) {
          //TODO skip null
					//pCI->dump();
					Value *val0 = pCI->getOperand(0);
					Value *val1 = pCI->getOperand(1);

					assert(pCI->getNumOperands() == 2);

					auto *const0 = dyn_cast<ConstantPointerNull>(val0);
					auto *const1 = dyn_cast<ConstantPointerNull>(val1);

					//if (const0) {
					//	errs() << "Found null ptr0: "; val0->dump();
					//}

					//if (const1) {
					//	errs() << "Found null ptr1: "; val1->dump();
					//}

					Type *ty0 = pCI->getOperand(0)->getType();
					Type *ty1 = pCI->getOperand(1)->getType();
					if (PointerType *pty = dyn_cast<PointerType>(ty0)) {
						if (!const0) {
					//pCI->dump();
							IRBuilder<> Builder(&I);
							auto castA = Builder.CreateCast(Instruction::BitCast, val0, Type::getInt8PtrTy(*C));
							auto xpacm = Intrinsic::getDeclaration(F.getParent(), Intrinsic::wyfy_xpacm, arg_type);
							auto callA = Builder.CreateCall(xpacm, {castA}, ""); 
							auto castB = Builder.CreateCast(Instruction::BitCast, callA, val0->getType());

							pCI->setOperand(0, castB);
							function_modified = true;
						}
					}

					if (PointerType *pty = dyn_cast<PointerType>(ty1)) {
						if (!const1) {
					//pCI->dump();
							IRBuilder<> Builder(&I);
							auto castA = Builder.CreateCast(Instruction::BitCast, val1, Type::getInt8PtrTy(*C));
							auto xpacm = Intrinsic::getDeclaration(F.getParent(), Intrinsic::wyfy_xpacm, arg_type);
							auto callA = Builder.CreateCall(xpacm, {castA}, ""); 
							auto castB = Builder.CreateCast(Instruction::BitCast, callA, val1->getType());

							pCI->setOperand(1, castB);
							function_modified = true;
						}
					}
				//} else if (auto pCI = dyn_cast<FCmpInst>(&I)) {
				//	//pCI->dump();
				//	Value *val0 = pCI->getOperand(0);
				//	Value *val1 = pCI->getOperand(1);

				//	assert(pCI->getNumOperands() == 2);

				//	Type *ty0 = pCI->getOperand(0)->getType();
				//	Type *ty1 = pCI->getOperand(1)->getType();
				//	if (PointerType *pty = dyn_cast<PointerType>(ty0)) {
				//	pCI->dump();
				//		IRBuilder<> Builder(&I);
				//		auto castA = Builder.CreateCast(Instruction::BitCast, val0, Type::getInt8PtrTy(*C));
				//		auto xpacm = Intrinsic::getDeclaration(F.getParent(), Intrinsic::wyfy_xpacm, arg_type);
				//		auto callA = Builder.CreateCall(xpacm, {castA}, ""); 
				//		auto castB = Builder.CreateCast(Instruction::BitCast, callA, val0->getType());

				//		pCI->setOperand(0, castB);
				//		function_modified = true;
				//	}

				//	if (PointerType *pty = dyn_cast<PointerType>(ty1)) {
				//	pCI->dump();
				//		IRBuilder<> Builder(&I);
				//		auto castA = Builder.CreateCast(Instruction::BitCast, val1, Type::getInt8PtrTy(*C));
				//		auto xpacm = Intrinsic::getDeclaration(F.getParent(), Intrinsic::wyfy_xpacm, arg_type);
				//		auto callA = Builder.CreateCall(xpacm, {castA}, ""); 
				//		auto castB = Builder.CreateCast(Instruction::BitCast, callA, val1->getType());

				//		pCI->setOperand(1, castB);
				//		function_modified = true;
				//	}
				//} else if (auto pAI = dyn_cast<BinaryOperator>(&I)) {
				//	//pAI->dump();
				//	Type *ty0 = pAI->getOperand(0)->getType();
				//	Type *ty1 = pAI->getOperand(1)->getType();

				//	if (PointerType *pty = dyn_cast<PointerType>(ty0)) {
        //    errs() << "[1] Found Binary Operator:"; pAI->dump();
        //  }

				//	if (PointerType *pty = dyn_cast<PointerType>(ty1)) {
        //    errs() << "[2] Found Binary Operator:"; pAI->dump();
        //  }
        //} else if (auto pBCI = dyn_cast<BitCastInst>(&I)) {

        //  Type *src_ty = pBCI->getSrcTy();
        //  Type *dst_ty = pBCI->getDestTy();
				//	if (PointerType *pty = dyn_cast<PointerType>(src_ty)) {
        //    if (dyn_cast<PointerType>(dst_ty)) {
        //    } else {
        //      errs() << "Found BitCastInst:"; pBCI->dump();
        //    }
        //  } 
        }
			}
		}
	}

	return function_modified;
}

bool AOSReachTestPass::doReachabilityTest(AOSNode *node) {
	if (!WYFY_FT)
		return true;

  list<AOSNode *> node_list;
  set<AOSNode *> visit_set;

	if (node == nullptr)
		return false;
	assert(node != nullptr);

	node_list.push_back(node);
  visit_set.insert(node);

	//errs() << "Print node!\n";
	//printNode(node);

  while (!node_list.empty()) {
    node = node_list.front();
    node_list.pop_front();

		if (node->isTainted())
			return true;

    for (auto it = node->children.begin(); it != node->children.end(); it++) {
      if (visit_set.find(*it) == visit_set.end()) {
        visit_set.insert(*it);
        node_list.push_back(*it);
				//errs() << "Print node!\n";
				//printNode(*it);
      }
    }
  }

	return false;
}

bool AOSReachTestPass::handleAlloca(Function *pF, AllocaInst *pAI) {
	if (Baseline || AOS)
		return false;

	Type *ty = pAI->getAllocatedType();
  auto size = pAI->getAllocationSizeInBits(*DL);
	if (size == llvm::None)
		return false;

	set<Type *> type_set;
	type_set = getStructTypes(ty, type_set);

	//errs() << "getStructTypes of: ";
	//ty->dump();
	//for (auto x: type_set)
	//	x->dump();
	//errs() << "-------------------\n";

	Value *arg = ConstantInt::get(Type::getInt64Ty(*C), (*size) / 8);

  std::vector<Type *> arg_type;
  Type *retType = Type::getInt8PtrTy(*C);
  arg_type.push_back(retType);

	// Alloc (pacma / bndstr)
  IRBuilder<> Builder(pAI->getNextNode());
  auto castA = dyn_cast<Instruction>(Builder.CreateCast(Instruction::BitCast, pAI, Type::getInt8PtrTy(*C)));
  auto pacma = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_pacma, arg_type);
  auto bndstr = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_bndstr, arg_type);
  auto callA = Builder.CreateCall(pacma, {castA, arg}, ""); 
  auto callB = Builder.CreateCall(bndstr, {callA, arg}, "");
  auto castB = Builder.CreateCast(Instruction::BitCast, callB, pAI->getType());

	pAI->replaceAllUsesWith(castB);
  castA->setOperand(0, pAI);

  // Dealloc (bndclr)
	bool chk = false;
	Instruction *pIB = nullptr;

	for (auto &BB : *pF) {
		for (auto &I : BB) {		
			if (dyn_cast<ReturnInst>(&I)) {
				pIB = &I;
				chk = true;
				break;
			}
		}

		if (chk)
			break;
	}
	assert(chk);
  IRBuilder<> BuilderB(pIB);
				
  //auto &BBB = pF->back();
  ////auto &IB = BBB.back();
	//Instruction *pIB = &(BBB.back());

	//while (1) {
	//	if (ReturnInst *pRI = dyn_cast<ReturnInst>(pIB))
	//		break;

	//	pIB = pIB->getPrevNode();

	//	if (!pIB
	//}

  //IRBuilder<> BuilderB(pIB);

	//auto bndclr = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_bndclr);
	//BuilderB.CreateCall(bndclr, {callB}, "");

	if ((WYFY_F || WYFY_FT) && IsStructTyWithArray(ty)) {
		FunctionType *FuncTypeB = FunctionType::get(Type::getVoidTy(*C), {Type::getInt8PtrTy(*C), Type::getInt64Ty(*C)}, false);
		Constant *clear = pF->getParent()->getOrInsertFunction("wyfy_clear_elements_stack", FuncTypeB);
		BuilderB.CreateCall(clear, {callB, arg});

		handleStruct(pF, pAI, type_set);
	} else {
	  auto bndclr = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_bndclr);
	  BuilderB.CreateCall(bndclr, {callB}, "");
	}

	return true;
}

bool AOSReachTestPass::handleMalloc(Function *pF, Instruction *pI) {
	if (Baseline)
		return false;

  //Function *called_func = pCI->getCalledFunction();
	Function *called_func = nullptr;

	if (CallInst *pCI = dyn_cast<CallInst>(pI)) {
		called_func = pCI->getCalledFunction();

		std::vector<Type *> arg_type;
		arg_type.push_back(pCI->getType());
		auto arg0 = pCI->getArgOperand(0);

		if (called_func && (called_func->getName() == "malloc" ||
								called_func->getName() == "_Znwm" /* new */ ||
								called_func->getName() == "_Znam" /* new[] */)) {

			IRBuilder<> Builder(pCI->getNextNode());
			auto pacma = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_pacma, arg_type);
			auto bndstr = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_bndstr, arg_type);
			auto callA = Builder.CreateCall(pacma, {pCI, arg0}, "");
			auto callB = Builder.CreateCall(bndstr, {callA, arg0}, "");
			pCI->replaceAllUsesWith(callB);
			callA->setOperand(0, pCI);
		} else if (called_func && called_func->getName() == "calloc") {
			auto arg1 = pCI->getArgOperand(1);

			IRBuilder<> Builder_prev(pCI);
			Value *res = Builder_prev.CreateMul(arg0, arg1);

			IRBuilder<> Builder(pCI->getNextNode());
			auto pacma = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_pacma, arg_type);
			auto bndstr = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_bndstr, arg_type);
			auto callA = Builder.CreateCall(pacma, {pCI, res}, ""); 
			auto callB = Builder.CreateCall(bndstr, {callA, res}, "");
			pCI->replaceAllUsesWith(callB);
			callA->setOperand(0, pCI);
		} else if (called_func && called_func->getName() == "realloc") {
			auto arg1 = pCI->getArgOperand(1);

			IRBuilder<> BuilderA(pCI);
			FunctionType *FuncType = FunctionType::get(Type::getInt8PtrTy(*C), {Type::getInt8PtrTy(*C), Type::getInt64Ty(*C)}, false);
			Constant *handle = pF->getParent()->getOrInsertFunction("wyfy_before_realloc", FuncType);
			auto call = BuilderA.CreateCall(handle, {arg0, arg1});
			pCI->setOperand(0, call);

			IRBuilder<> BuilderB(pCI->getNextNode());
			FunctionType *FuncTypeB = FunctionType::get(Type::getInt8PtrTy(*C), {Type::getInt8PtrTy(*C), Type::getInt64Ty(*C)}, false);
			Constant *handleB = pF->getParent()->getOrInsertFunction("wyfy_after_realloc", FuncTypeB);
			auto callB = BuilderB.CreateCall(handleB, {pCI, arg1});
			pCI->replaceAllUsesWith(callB);
			callB->setOperand(0, pCI);
			//auto pacma = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_pacma, arg_type);
			//auto bndstr = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_bndstr, arg_type);
			//auto callC = BuilderB.CreateCall(pacma, {pCI, arg1}, ""); 
			//auto callD = BuilderB.CreateCall(bndstr, {callC, arg1}, "");
			//pCI->replaceAllUsesWith(callD);
			//callC->setOperand(0, pCI);
		}

		set<Type *> type_set;

		if (WYFY_F || WYFY_FT)
			handleStruct(pF, pCI, type_set);

	} else if (InvokeInst *pII = dyn_cast<InvokeInst>(pI)) {
		called_func = pII->getCalledFunction();

		std::vector<Type *> arg_type;
		arg_type.push_back(pII->getType());
		auto arg0 = pII->getArgOperand(0);

		if (called_func && (called_func->getName() == "malloc" ||
								called_func->getName() == "_Znwm" /* new */ ||
								called_func->getName() == "_Znam" /* new[] */)) {

      auto pBB = pII->getNormalDest();
      IRBuilder<> Builder(&(pBB->front()));
			auto pacma = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_pacma, arg_type);
			auto bndstr = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_bndstr, arg_type);
			auto callA = Builder.CreateCall(pacma, {pII, arg0}, "");
			auto callB = Builder.CreateCall(bndstr, {callA, arg0}, "");
			pII->replaceAllUsesWith(callB);
			callA->setOperand(0, pII);
		} else if (called_func && called_func->getName() == "calloc") {
			auto arg1 = pII->getArgOperand(1);

			IRBuilder<> Builder_prev(pII);
			Value *res = Builder_prev.CreateMul(arg0, arg1);

			IRBuilder<> Builder(pII->getNextNode());
			auto pacma = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_pacma, arg_type);
			auto bndstr = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_bndstr, arg_type);
			auto callA = Builder.CreateCall(pacma, {pII, res}, ""); 
			auto callB = Builder.CreateCall(bndstr, {callA, res}, "");
			pII->replaceAllUsesWith(callB);
			callA->setOperand(0, pII);
		} else if (called_func && called_func->getName() == "realloc") {
			auto arg1 = pII->getArgOperand(1);

			IRBuilder<> BuilderA(pII);
			FunctionType *FuncType = FunctionType::get(Type::getInt8PtrTy(*C), {Type::getInt8PtrTy(*C), Type::getInt64Ty(*C)}, false);
			Constant *handle = pF->getParent()->getOrInsertFunction("wyfy_before_realloc", FuncType);
			auto call = BuilderA.CreateCall(handle, {arg0, arg1});
			pII->setOperand(0, call);

			IRBuilder<> BuilderB(pII->getNextNode());
			FunctionType *FuncTypeB = FunctionType::get(Type::getInt8PtrTy(*C), {Type::getInt8PtrTy(*C), Type::getInt64Ty(*C)}, false);
			Constant *handleB = pF->getParent()->getOrInsertFunction("wyfy_after_realloc", FuncTypeB);
			auto callB = BuilderB.CreateCall(handleB, {pII, arg1});
			pII->replaceAllUsesWith(callB);
			callB->setOperand(0, pII);
			//auto pacma = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_pacma, arg_type);
			//auto bndstr = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_bndstr, arg_type);
			//auto callC = BuilderB.CreateCall(pacma, {pII, arg1}, ""); 
			//auto callD = BuilderB.CreateCall(bndstr, {callC, arg1}, "");
			//pCI->replaceAllUsesWith(callD);
			//callC->setOperand(0, pCI);
		}

		set<Type *> type_set;

		if (WYFY_F || WYFY_FT)
			handleStruct(pF, pII, type_set);

	}


  return true;
}

bool AOSReachTestPass::handleFree(Function *pF, Instruction *pI) {
	if (Baseline)
		return false;

	if (CallInst *pCI = dyn_cast<CallInst>(pI)) {
		auto arg = pCI->getArgOperand(0);
		std::vector<Type *> arg_type;
		arg_type.push_back(arg->getType());
		IRBuilder<> BuilderA(pCI);

		if (WYFY_F || WYFY_FT) {
			FunctionType *FuncTypeA = FunctionType::get(Type::getInt8PtrTy(*C), {Type::getInt8PtrTy(*C)}, false);
			//FunctionType *FuncTypeA = FunctionType::get(Type::getVoidTy(*C), {Type::getInt8PtrTy(*C)}, false);
			Constant *clear = pF->getParent()->getOrInsertFunction("wyfy_clear_elements_heap", FuncTypeA);
			auto callA = BuilderA.CreateCall(clear, {arg});
			auto xpacm = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_xpacm, arg_type);
			auto callB = BuilderA.CreateCall(xpacm, {callA}, "");
			pCI->setOperand(0, callB);
		} else {
			auto bndclr = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_bndclr);
			BuilderA.CreateCall(bndclr, {arg}, "");
			auto xpacm = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_xpacm, arg_type);
			auto callA = BuilderA.CreateCall(xpacm, {arg}, "");
			pCI->setOperand(0, callA);
		}
	} else if (InvokeInst *pII = dyn_cast<InvokeInst>(pI)) {
		auto arg = pII->getArgOperand(0);
		std::vector<Type *> arg_type;
		arg_type.push_back(arg->getType());
		IRBuilder<> BuilderA(pII);

		if (WYFY_F || WYFY_FT) {
			FunctionType *FuncTypeA = FunctionType::get(Type::getInt8PtrTy(*C), {Type::getInt8PtrTy(*C)}, false);
			//FunctionType *FuncTypeA = FunctionType::get(Type::getVoidTy(*C), {Type::getInt8PtrTy(*C)}, false);
			Constant *clear = pF->getParent()->getOrInsertFunction("wyfy_clear_elements_heap", FuncTypeA);
			auto callA = BuilderA.CreateCall(clear, {arg});
			auto xpacm = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_xpacm, arg_type);
			auto callB = BuilderA.CreateCall(xpacm, {callA}, "");
			pII->setOperand(0, callB);
		} else {
			auto bndclr = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_bndclr);
			BuilderA.CreateCall(bndclr, {arg}, "");
			auto xpacm = Intrinsic::getDeclaration(pF->getParent(), Intrinsic::wyfy_xpacm, arg_type);
			auto callA = BuilderA.CreateCall(xpacm, {arg}, "");
			pII->setOperand(0, callA);
		}
	}

  return true;
}

bool AOSReachTestPass::handleStruct(Function *pF, Value *pV, set<Type *> type_set) {
  list<AOSNode *> node_list;
  set<AOSNode *> visit_set;
	AOSNode *node = value_map[pV];

	node_list.push_back(node);
  visit_set.insert(node);

  while (!node_list.empty()) {
    node = node_list.front();
    node_list.pop_front();

		// Insert children
		for (auto child: node->children) {
      if (visit_set.find(child) == visit_set.end()) {
        visit_set.insert(child);
        node_list.push_back(child);
      }
    }

		if (PointerType *pty = dyn_cast<PointerType>(node->ty)) {
			Type *ety = pty->getElementType();

			//if (!ety->isArrayTy() && !ety->isStructTy())
			if (!ety->isArrayTy() && !IsStructTyWithArray(ety))
				continue;

			// taint check
			if (WYFY_FT && !doReachabilityTest(node))
				continue;

			for (auto const &it: node->aliases) {
				if (auto pGEP = dyn_cast<GetElementPtrInst>(it.first)) {
					// Skip if numIndices == 1 && index == 0
					if (pGEP->getNumIndices() == 1) {
						ConstantInt *idx = dyn_cast<ConstantInt>(*(pGEP->idx_begin()));

						//if (idx && idx->getSExtValue() == 0)
							continue;
					}

          assert (pGEP->getNumIndices() < 3);

					Type *src_ty = pGEP->getSourceElementType();

					if (!type_set.empty() && type_set.find(src_ty) == type_set.end())
						continue;

					//if (!IsStructTy(src_ty) || pGEP->getPointerOperandType() != pV->getType() ||
					//if (!src_ty->isStructTy() || pGEP->getPointerOperandType() != pV->getType() ||
					//if (!src_ty->isStructTy() ||
					//if (!IsStructTy(src_ty) ||
					if (!IsStructTyWithArray(src_ty) ||
							sign_set.find(pGEP) != sign_set.end())
						continue;

					if (!(pGEP->getResultElementType()->isStructTy() ||
								pGEP->getResultElementType()->isArrayTy())) {
						pGEP->dump();
						pGEP->getType()->dump();
						assert(pGEP->getResultElementType()->isStructTy() || pGEP->getResultElementType()->isArrayTy());
					}

					//errs() << "pGEP->dump(): "; pGEP->dump();
					// insert pacma
					sign_set.insert(pGEP);

					auto size = DL->getTypeSizeInBits(pGEP->getResultElementType());
					Value *arg = ConstantInt::get(Type::getInt64Ty(*C), size / 8);
					auto base = pGEP->getPointerOperand();

					IRBuilder<> Builder(pGEP->getNextNode());
					FunctionType *FuncTypeA = FunctionType::get(Type::getInt8PtrTy(*C), {Type::getInt8PtrTy(*C), Type::getInt8PtrTy(*C), Type::getInt64Ty(*C), Type::getInt64Ty(*C)}, false);
					auto typeIdConstant = getTypeIDConstantFrom(*(pGEP->getType()), *C);
					auto castA = Builder.CreateCast(Instruction::BitCast, base, Type::getInt8PtrTy(*C));
					auto castB = dyn_cast<Instruction>(Builder.CreateCast(Instruction::BitCast, pGEP, Type::getInt8PtrTy(*C)));
					Constant *promote = pF->getParent()->getOrInsertFunction("wyfy_sign_element", FuncTypeA);
					auto callA = Builder.CreateCall(promote, {castA, castB, arg, typeIdConstant});
					auto castC = Builder.CreateCast(Instruction::BitCast, callA, pGEP->getType());

					pGEP->replaceAllUsesWith(castC);
					castB->setOperand(0, pGEP);
				}
			}
		}
  }
}

bool AOSReachTestPass::IsArrayTy(Type *ty) {
	if (!ty->isArrayTy())
		return false;

	while (ty->isArrayTy()) {
		ty = ty->getArrayElementType();

		if (ty->isStructTy())
			return false;
	}

	return true;
}

bool AOSReachTestPass::IsStructTyWithArray(Type *ty) {
	while (ty->isArrayTy())
		ty = ty->getArrayElementType();

	if (auto str_ty = dyn_cast<StructType>(ty)) {
		for (auto it = str_ty->element_begin(); it != str_ty->element_end(); it++) {
			Type *ety = (*it);

			if (ety->isArrayTy())
				return true;

			if (ety->isStructTy() && IsStructTyWithArray(ety))
				return true;
		}
	}

	return false;
}

set<Type *> AOSReachTestPass::getStructTypes(Type *ty, set<Type *> type_set) {
	while (ty->isArrayTy())
		ty = ty->getArrayElementType();

	if (auto str_ty = dyn_cast<StructType>(ty)) {
		for (auto it = str_ty->element_begin(); it != str_ty->element_end(); it++) {
			Type *ety = (*it);

			if (ety->isArrayTy())
				type_set.insert(str_ty);

			//if (ety->isStructTy())
			if (IsStructTyWithArray(ety))
				type_set = getStructTypes(ety, type_set);
		}
	}

	return type_set;
}

bool AOSReachTestPass::IsStructTy(Type *ty) {
	if (ty->isStructTy())
		return true;
	else if (!ty->isArrayTy())
		return false;

	while (ty->isArrayTy()) {
		ty = ty->getArrayElementType();

		if (ty->isStructTy())
			return true;
	}

	return false;
}

void AOSReachTestPass::init(Module &M) {
	for (auto &F : M) {
		if (&F && F.getName() == "main") {
		  C = &F.getContext();
			DL = &F.getParent()->getDataLayout();

			return;
		}
	}

	for (auto &F : M) {
		if (&F) {
		  C = &F.getContext();
			DL = &F.getParent()->getDataLayout();

			return;
		}
	}
}

// auto typeIdConstant = PARTS::getTypeIDConstantFrom(*calledValueType, F.getContext());
// auto paced = Builder.CreateCall(autcall, { calledValue, typeIdConstant }, "");

void AOSReachTestPass::buildTypeString(const Type *T, llvm::raw_string_ostream &O) {
  if (T->isPointerTy()) {
    O << "ptr.";
    buildTypeString(T->getPointerElementType(), O);
  } else if (T->isStructTy()) {
		auto sty = dyn_cast<StructType>(T);
		std::regex e("^(\\w+\\.\\w+)(\\.\\w+)?$");

		if (sty->isLiteral()) {
			O << std::regex_replace("str.", e, "$1");
		} else {
			auto structName = dyn_cast<StructType>(T)->getStructName();
			O << std::regex_replace(structName.str(), e, "$1");
		}
  } else if (T->isArrayTy()) {
    O << "arr.";
    buildTypeString(T->getArrayElementType(), O);
  } else if (T->isFunctionTy()) {
    auto FuncTy = dyn_cast<FunctionType>(T);
    O << "f.";
    buildTypeString(FuncTy->getReturnType(), O);

    for (auto p = FuncTy->param_begin(); p != FuncTy->param_end(); p++) {
      buildTypeString(*p, O);
    }
  } else if (T->isVectorTy()) {
    O << "vec." << T->getVectorNumElements();
    buildTypeString(T->getVectorElementType(), O);
  } else if (T->isVoidTy()) {
    O << "v";
  } else {
    /* Make sure we've handled all cases we want to */
    assert(T->isIntegerTy() || T->isFloatingPointTy());
    T->print(O);
  }
}

std::map<const Type *, uint64_t> TypeIDCache;

uint64_t AOSReachTestPass::getTypeIDFor(const Type *T) {
  if (!T->isPointerTy())
    return 0; // Not a pointer, hence no type ID for this one

  // TODO: This should perform caching, so calling the same Type will not
  // reprocess the stuff. Use a Dictionary-like ADT is suggested.
  decltype(TypeIDCache)::iterator id;
  if ((id = TypeIDCache.find(T)) != TypeIDCache.end())
    return id->second;

  uint64_t theTypeID = 0;
  std::string buf;
  llvm::raw_string_ostream typeIdStr(buf);

  buildTypeString(T, typeIdStr);
  typeIdStr.flush();

  // Prepare SHA3 generation
  auto rawBuf = buf.c_str();
  mbedtls_sha3_context sha3_context;
  mbedtls_sha3_type_t sha3_type = MBEDTLS_SHA3_256;
  mbedtls_sha3_init(&sha3_context);

  // Prepare input and output variables
  auto *input = reinterpret_cast<const unsigned char *>(rawBuf);
  auto *output = new unsigned char[32]();

  // Generate hash
  auto result = mbedtls_sha3(input, buf.length(), sha3_type, output);
  if (result != 0)
    llvm_unreachable("SHA3 hashing failed :(");
  memcpy(&theTypeID, output, sizeof(theTypeID));
  delete[] output;

  TypeIDCache.emplace(T, theTypeID);

  return theTypeID;
}

Constant *AOSReachTestPass::getTypeIDConstantFrom(const Type &T, LLVMContext &C) {
  return Constant::getIntegerValue(Type::getInt64Ty(C),
                                   //APInt(64, getTypeIDFor(&T) & 0xFFFF));
                                   APInt(64, getTypeIDFor(&T)));

  //return Constant::getIntegerValue(Type::getInt64Ty(C),
  //                                 APInt(64, 0));
}

void AOSReachTestPass::printNode(AOSNode *node) {
  errs() << "Print node!\n";
	errs() << "-- "; node->ty->dump();
	for (auto it = node->aliases.begin(); it != node->aliases.end(); it ++) {
		errs() << "-- "; it->first->dump();
	}
}

