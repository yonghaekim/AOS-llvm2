#include "llvm/AOS/AOSPointerAliasPass.h"

char AOSPointerAliasPass::ID = 0;

static RegisterPass<AOSPointerAliasPass> X("aos-alias", "AOS pointer alias pass");

Pass *llvm::AOS::createAOSPointerAliasPass() { return new AOSPointerAliasPass(); }

bool AOSPointerAliasPass::runOnModule(Module &M) {
	//unsigned buf_cnt = 0;
	//unsigned buf_sum = 0;

	//for (auto &G : M.getGlobalList()) {
	//	GlobalVariable *pGV = dyn_cast<GlobalVariable>(&G);
	//	Type *ty = pGV->getType()->getElementType();

	//	// Skip constant GV
	//	if (ty->isArrayTy() || ty->isStructTy()) {
	//		const DataLayout &DL = M.getDataLayout();
	//		auto size = DL.getTypeSizeInBits(ty);
	//		buf_cnt++;
	//		buf_sum += (size / 8);
	//		//errs() << "Size: " << (size / 8) << "\n";
	//	}
  //}

	//for (auto &F : M) {
	//	for (auto &BB : F) {
	//		for (auto &I : BB) {
  //      switch(I.getOpcode()) {
  //        case Instruction::Alloca:
  //        {
  //          AllocaInst *pAI = dyn_cast<AllocaInst>(&I);
	//					Type *ty = pAI->getAllocatedType();

	//					if (ty->isArrayTy() || ty->isStructTy()) {
	//						const DataLayout &DL = M.getDataLayout();
	//					  auto size = pAI->getAllocationSizeInBits(DL);				
	//						buf_cnt++;
	//						buf_sum += ((*size) / 8);
	//						//errs() << "Size: " << ((*size) / 8) << "\n";
	//					}
	//				}
	//				default:
	//					break;
	//			}
	//		}
	//	}
	//}

	//errs() << "buf_cnt: " << buf_cnt << "\n";
	//errs() << "buf_sum: " << buf_sum << "\n";
	//errs() << "buf_avg: " << (buf_sum / buf_cnt) << "\n";

	//return false;


	//////// Don't touch 

	errs() << "Start pointer alias analysis pass!\n";

  // preprocess gv struct // need to take care at the user iteration too

	getFunctionsFromCallGraph(M);

	root_node = new AOSNode();

  //handleLoads(M);
  //assert(false);

	// Handle global variables
	handleGlobalVariables(M);

	handleGlobalPointers(M);

	for (auto pF : func_list) {
		if (find(uncalled_list.begin(), uncalled_list.end(), pF) != uncalled_list.end())
			handleArguments(pF);

		std::list<std::vector<BasicBlock *>> SCCBBs_list;

		for (scc_iterator<Function*> I = scc_begin(pF); I != scc_end(pF); ++I)
			SCCBBs_list.push_back(*I);

		while (!SCCBBs_list.empty()) {
			const std::vector<BasicBlock *> SCCBBs = SCCBBs_list.back();
			SCCBBs_list.pop_back();

			// Obtain the vector of BBs in this SCC and print it out.
			for (std::vector<BasicBlock *>::const_iterator BBI = SCCBBs.begin();
																										 BBI != SCCBBs.end(); ++BBI) {
				// Handle AllocaInst and CallInst (malloc)
				handleInstructions(*BBI);
			}
		}
	}

	while (!work_list.empty()) {
		AOSAlias *alias = work_list.front();
		work_list.pop_front();
		//errs() << "Working on this:\n";
		//alias->getPtr()->dump();
		getPointerAliases(alias);
	}



	//dump();

  return false;
}

void AOSPointerAliasPass::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.setPreservesAll();
	//AU.addRequired<AOSBBCounterPass>();
	AU.addRequired<CallGraphWrapperPass>();
}

AOSPointerAliasPass::AOSNode* AOSPointerAliasPass::getRootNode() {
	return root_node;
}

map<Value *, AOSPointerAliasPass::AOSNode *> AOSPointerAliasPass::getValueMap() {
	return value_map;
}

list<Function *> AOSPointerAliasPass::getUncalledList() {
	return uncalled_list;
}

void AOSPointerAliasPass::handleArguments(Function *pF) {
	for (auto arg = pF->arg_begin(); arg != pF->arg_end(); arg++) {
		if (!arg->getType()->isPointerTy())
			continue;

		AOSNode *new_node = new AOSNode();
		AOSAlias *alias = new AOSAlias(arg, new_node);
		Type *ty = arg->getType();

		new_node->setType(ty);
		new_node->addAlias(alias);

		work_list.push_back(alias);
		root_node->addChild(new_node);
		new_node->addParent(root_node);
		value_map[arg] = new_node;
	}
}

bool isPointerToPointer(const Value* V) {
    const Type* T = V->getType();
    return T->isPointerTy() && T->getContainedType(0)->isPointerTy();
}

bool IsStructTyWithArray(Type *ty) {
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

void AOSPointerAliasPass::handleLoadPtrOperand(Value *pV) {
  map<BasicBlock *, list<LoadInst *>> load_map;
  map<BasicBlock *, list<StoreInst *>> store_map;

  for (auto pU: pV->users()) {
    if (Instruction *pI = dyn_cast<Instruction>(pU)) {
      BasicBlock *pBB = pI->getParent();

      if (LoadInst *pLI = dyn_cast<LoadInst>(pU)) {
        load_map[pBB].push_back(pLI);
      } else if (StoreInst *pSI = dyn_cast<StoreInst>(pU)) {
        store_map[pBB].push_back(pSI);
      }
    }
  }

  for (auto &x: load_map) {
    auto pBB = x.first;
    auto load_list = x.second;
    //
    if (store_map[pBB].empty()) {
      LoadInst *pLI;
      bool chk = false;
      // Find the youngest inst.
      for (auto &I: *pBB) {
        if (LoadInst *pLI2 = dyn_cast<LoadInst>(&I)) {
          if (find(load_list.begin(), load_list.end(), pLI2) != load_list.end()) {
            pLI = pLI2;
            chk = true;
            break;
          }
        }
      }
      assert(chk);

      while (!load_list.empty()) {
        LoadInst *pLI2 = load_list.front();
        load_list.pop_front(); 
        if (pLI != pLI2) {
          pLI2->replaceAllUsesWith(pLI);
          pLI2->eraseFromParent();
        }
      }

      map<int, list<GetElementPtrInst *>> gep_map;

      for (auto pU: pLI->users()) {
        if (GetElementPtrInst *pGEP = dyn_cast<GetElementPtrInst>(pU)) {
          //assert(pGEP->getParent() == pBB);
          if (pGEP->getParent() != pBB)
            continue;
          //pGEP->dump();

          if (pGEP->getNumIndices() != 2)
            continue;

          auto itr = pGEP->idx_begin();
					ConstantInt *idx = dyn_cast<ConstantInt>(*itr);
					if (!idx || idx->getSExtValue() != 0)
            continue;

					idx = dyn_cast<ConstantInt>(*(++itr));

					if (!idx)
            continue;

          int num = idx->getSExtValue();

          gep_map[num].push_back(pGEP);
          //pGEP->dump();
        }
      }

      for (auto &xx: gep_map) {
        auto gep_list = xx.second;

        GetElementPtrInst *pGEP;
        bool chk = false;

        // Find the youngest inst.
        for (auto &I: *pBB) {
          if (GetElementPtrInst *pGEP2 = dyn_cast<GetElementPtrInst>(&I)) {
            if (find(gep_list.begin(), gep_list.end(), pGEP2) != gep_list.end()) {
              pGEP = pGEP2;
              chk = true;
              break;
            }
          }
        }
        assert(chk);

        while (!gep_list.empty()) {
          GetElementPtrInst *pGEP2 = gep_list.front();
          gep_list.pop_front(); 
          if (pGEP != pGEP2) {
            pGEP2->replaceAllUsesWith(pGEP);
            pGEP2->eraseFromParent();
          }
        }
      }
    }
  }

}

void AOSPointerAliasPass::handleLoads(Module &M) {
  set<Value *> load_set;

  for (auto &F : M) {
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (LoadInst *pLI = dyn_cast<LoadInst>(&I)) {

          auto ptrOp = pLI->getPointerOperand();

          if (isPointerToPointer(ptrOp)) {
            Type *ty = pLI->getType()->getContainedType(0);
            if (!IsStructTyWithArray(ty))
              continue;

            if (load_set.find(ptrOp) != load_set.end())
             continue;

            load_set.insert(ptrOp);
            handleLoadPtrOperand(ptrOp);
          }
        }
      }
    }
  }
}

void AOSPointerAliasPass::handleGlobalVariables(Module &M) {
	for (auto &G : M.getGlobalList()) {
		GlobalVariable *pGV = dyn_cast<GlobalVariable>(&G);

		// Skip constant GV
		if (pGV->isConstant() && pGV->getName().find(".str") == 0)
			continue;

		if (pGV->use_empty() || pGV->getLinkage() == GlobalValue::LinkOnceODRLinkage)
			continue;

    if (!pGV->hasInitializer() || pGV->isDeclaration())
      continue;

    // Convert operators to instructions
    handleOperators(pGV);

		AOSNode *new_node = new AOSNode();
		AOSAlias *alias = new AOSAlias(pGV, new_node);

		new_node->setType(pGV->getType());
		new_node->addAlias(alias);

		work_list.push_back(alias);
		root_node->addChild(new_node);
		new_node->addParent(root_node);
		value_map[pGV] = new_node;
	}
}

void AOSPointerAliasPass::getUsersOfGV(Value *pV, list<Operator *> op_list) {
	for (auto pU: pV->users()) {
		if (auto *pI = dyn_cast<Instruction>(pU)) {
			//inst_map[pI] = op_list;
			inst_map[pI].push_back(op_list);
		} else if (auto *pOp = dyn_cast<Operator>(pU)) {
			list<Operator *> op_list_new = op_list;
			op_list_new.push_back(pOp);
			getUsersOfGV(pOp, op_list_new);
		}
	}
}

void AOSPointerAliasPass::handleOperators(GlobalVariable *pGV) {
  //errs() << "handle Ops of: "; pGV->dump();
	list<Operator *> op_list;

	getUsersOfGV(pGV, op_list);

  map<Function *, Value *> ptr_map;
  map<Function *, map<pair<Value *, int>, Value *>> gep_map;
  map<Function *, map<pair<Value *, Type *>, Value *>> bc_map;
  map<Function *, map<pair<Value *, Type *>, Value *>> pti_map;

  list<Value *> insts_to_erase;

	for (auto &x: inst_map) {
		auto pI = x.first;
		auto list_list = x.second;
		auto pF = pI->getFunction();
		auto &BB = pF->front();
		auto &I = BB.front();

    for (auto list: list_list) {
      // This is an instruction not operator
      if (list.size() == 0)
        continue;

      Value *cur_val = ptr_map[pF];

      if (cur_val == nullptr) {
        IRBuilder<> Builder(&I);
        auto ptr = new GlobalVariable(*(pF->getParent()), pGV->getType(), false, GlobalVariable::ExternalLinkage,
                                      0, "aos_ptr" + to_string(ptr_map.size()));
        ConstantPointerNull* const_ptr = ConstantPointerNull::get(pGV->getType());
        ptr->setInitializer(pGV);

        auto pSI = Builder.CreateStore(pGV, ptr);
        auto pLI = Builder.CreateLoad(pGV->getType(), ptr, "");

        insts_to_erase.push_back(pLI);
        insts_to_erase.push_back(pSI);
        insts_to_erase.push_back(ptr);

        cur_val = pLI;
        ptr_map[pF] = cur_val;
      }

      for (auto pOp: list) {
        if (auto GEPOp = dyn_cast<GEPOperator>(pOp)) {

          // Workaround.. gep(0,0,0) <=> bitcast
          bool bcChk = false;
          for (auto it = GEPOp->idx_begin(); it != GEPOp->idx_end(); it++) {
            if (ConstantInt *idx = dyn_cast<ConstantInt>(*it)) {
                if (idx->getSExtValue() != 0) {
                  bcChk = true;
                  break;
                }
            } else {
              bcChk = true;
              break;
            }
          }

          if (!bcChk) {
            IRBuilder<> Builder(dyn_cast<Instruction>(cur_val)->getNextNode());
            cur_val = Builder.CreateCast(Instruction::BitCast, cur_val, GEPOp->getType());
            //errs() << "Found !bcChk: "; GEPOp->dump();
            //errs() << "(2): "; cur_val->dump();
            continue;
          }

          for (auto it = GEPOp->idx_begin(); it != GEPOp->idx_end(); it++) {
            if (it == GEPOp->idx_begin()) {

              if (GEPOp->getNumIndices() == 1) {
                vector<Value *> indices;
                indices.push_back(*it);
                IRBuilder<> Builder(dyn_cast<Instruction>(cur_val)->getNextNode());
                cur_val = Builder.CreateGEP(cur_val, indices);
                //errs() << "gep[3]: "; cur_val->dump();
              } else if (ConstantInt *idx = dyn_cast<ConstantInt>(*it)) {
                if (idx->getSExtValue() != 0) {
                  vector<Value *> indices;
                  indices.push_back(*it);
                  IRBuilder<> Builder(dyn_cast<Instruction>(cur_val)->getNextNode());
                  cur_val = Builder.CreateGEP(cur_val, indices);
                  //GEPOp->dump();
                  //assert(false);
                }
              } else {
                GEPOp->dump();
                assert(false);
              }

              continue;
            }

            vector<Value *> indices;
            indices.push_back(ConstantInt::get(Type::getInt64Ty(pF->getContext()), 0));
            indices.push_back(*it);

            ConstantInt *idx = dyn_cast<ConstantInt>(*it);
            Value *temp_val = gep_map[pF][make_pair(cur_val, idx->getSExtValue())];

            if (temp_val != nullptr) {
              cur_val = temp_val;
              //errs() << "gep[1]: "; cur_val->dump();
            } else {
              IRBuilder<> Builder(dyn_cast<Instruction>(cur_val)->getNextNode());
              temp_val = cur_val;
              cur_val = Builder.CreateGEP(cur_val, indices);
              gep_map[pF][make_pair(temp_val, idx->getSExtValue())] = cur_val;
              //errs() << "gep[2]: "; cur_val->dump();
            }
          }
        } else if (auto BCOp = dyn_cast<BitCastOperator>(pOp)) {
          Value *temp_val = bc_map[pF][make_pair(cur_val, BCOp->getDestTy())];

          //errs() << "BCOp->dump(): "; BCOp->dump();

          if (temp_val != nullptr) {
            cur_val = temp_val;

            assert(BCOp->getDestTy() == dyn_cast<BitCastOperator>(cur_val)->getDestTy());
            //errs() << "bc[1]: "; cur_val->dump();
          } else {
            IRBuilder<> Builder(dyn_cast<Instruction>(cur_val)->getNextNode());
            temp_val = cur_val;
            cur_val = Builder.CreateCast(Instruction::BitCast, cur_val, BCOp->getDestTy());
            bc_map[pF][make_pair(temp_val, BCOp->getDestTy())] = cur_val;
            //errs() << "bc[2]: "; cur_val->dump();
          }
        } else if (auto PTIOp = dyn_cast<PtrToIntOperator>(pOp)) {
          Value *temp_val = pti_map[pF][make_pair(cur_val, PTIOp->getType())];

          if (temp_val != nullptr) {
            cur_val = temp_val;
            //errs() << "pti[1]: "; cur_val->dump();
          } else {
            IRBuilder<> Builder(dyn_cast<Instruction>(cur_val)->getNextNode());
            temp_val = cur_val;
            cur_val = Builder.CreatePtrToInt(cur_val, PTIOp->getType());
            pti_map[pF][make_pair(temp_val, PTIOp->getType())] = cur_val;
            //errs() << "pti[2]: "; cur_val->dump();
          }
        } else {
          pOp->dump();
          assert(false);
        }
      }

      assert(cur_val != pGV);

      unsigned cnt = 0;
      bool chk = false;
      for (auto it = pI->op_begin(); it != pI->op_end(); it++) {
        if (dyn_cast<LandingPadInst>(pI))
          continue;

        if ((*it) == list.back()) {
          //errs() << "(1) pI->dump(): "; pI->dump();
          //errs() << "setOperand of: "; pI->dump();
          assert((*it)->getType() == cur_val->getType());

          pI->setOperand(cnt, cur_val);

          //errs() << "(2) pI->dump(): "; pI->dump();
          chk = true;
          break;
        }
          
        cnt++;
      }

      //assert(chk);
    }
	}

  for (auto &x: ptr_map)
    x.second->replaceAllUsesWith(pGV);

  while (!insts_to_erase.empty()) {
    auto *pV = insts_to_erase.front();
    insts_to_erase.pop_front();

    if (GlobalVariable *_pGV = dyn_cast<GlobalVariable>(pV))
      _pGV->eraseFromParent();
    else if (Instruction *_pI = dyn_cast<Instruction>(pV))
      _pI->eraseFromParent();
    else
      assert(false);
  }

	inst_map.clear();
}

void AOSPointerAliasPass::handleGlobalPointers(Module &M) {
	for (auto &G : M.getGlobalList()) {
		GlobalVariable *pGV = dyn_cast<GlobalVariable>(&G);

		// Skip constant GV
		if (pGV->isConstant() && pGV->getName().find(".str") == 0)
			continue;


    // Handle global pointer pointing to global variable
    if (pGV->hasInitializer()) {
      if (GlobalVariable *_pGV = dyn_cast<GlobalVariable>(pGV->getInitializer())) {
        if (AOSNode *_node = value_map[_pGV]) {
        	if (AOSNode *node = value_map[pGV]) {
	          _node->addMemEdge(node);
	          node->setMemUser(_node);
					}
        }
      }
    }
	}
}

void AOSPointerAliasPass::handleInstructions(BasicBlock *BB) {
	for (auto &I : *BB) {
		switch(I.getOpcode()) {
			case Instruction::Alloca:
			{
				AllocaInst *pAI = dyn_cast<AllocaInst>(&I);

				AOSNode *new_node = new AOSNode();
				AOSAlias *alias = new AOSAlias(pAI, new_node);

				new_node->setType(pAI->getType());
				new_node->addAlias(alias);

				//errs() << "pAI->dump(): "; pAI->dump();
				//errs() << "pAI->getType(): "; pAI->getType()->dump();

				work_list.push_back(alias);
				root_node->addChild(new_node);
				new_node->addParent(root_node);
				value_map[pAI] = new_node;

				break;
			}
			case Instruction::Invoke:
			case Instruction::Call:
			{
				Function *pF;
				Type *ty;

				if (CallInst *pCI = dyn_cast<CallInst>(&I)) {
					pF = pCI->getCalledFunction();
					ty = pCI->getType();
				} else if (InvokeInst *pII = dyn_cast<InvokeInst>(&I)) {
					pF = pII->getCalledFunction();
					ty = pII->getType();
				}

				if (!pF && ty->isPointerTy()) {
					AOSNode *new_node = new AOSNode();
					AOSAlias *alias = new AOSAlias(&I, new_node);

					new_node->setType(ty);
					new_node->addAlias(alias);

					work_list.push_back(alias);
					root_node->addChild(new_node);
					new_node->addParent(root_node);
					value_map[&I] = new_node;
				} if (pF && (pF->getName() == "malloc" ||
										pF->getName() == "calloc" ||
										pF->getName() == "realloc" ||
										pF->getName() == "_Znwm" /* new */ ||
										pF->getName() == "_Znam" /* new[] */)) {

					AOSNode *new_node = new AOSNode();
					AOSAlias *alias = new AOSAlias(&I, new_node);

					//if (pF->getName() == "_Znam") {
					//	errs() << "Found _Znam!\n";
					//	I.dump();
					//}

					new_node->setType(ty);
					new_node->addAlias(alias);

					work_list.push_back(alias);
					root_node->addChild(new_node);
					new_node->addParent(root_node);
					value_map[&I] = new_node;
				}

				break;
			}
			default:
				break;
		}
	}
}

void AOSPointerAliasPass::getPointerAliases(AOSAlias *alias) {
	AOSNode *cur_node = alias->getNode();
	Value *pV = alias->getPtr();

	//errs() << "Iterate for this value!-- ";
	//pV->dump();
	for (auto pU: pV->users()) {
		//errs() << "start_idx:" << cur_node->start_idx << "\n";
		if (auto pI = dyn_cast<Instruction>(pU)) {
    //pU->dump();
		//if (auto pI = dyn_cast<Operator>(pU)) {
			switch (pI->getOpcode()) {
				case Instruction::Invoke:
				case Instruction::Call:
				{
					//errs() << "Handle CallInst!\n-- ";
					//pI->dump();
					Value *arg = getArgument(pU, pV);

					if (arg) {
						if (AOSNode *node = value_map[arg]) {
							if (cur_node != node) {
								if (node->ty != cur_node->ty) {
									//printNode(node);
									//printNode(cur_node);
									arg->dump();
									pV->dump();
									pI->dump();
									node->ty->dump();
									errs() << "start_idx:" << cur_node->start_idx << "\n";
									cur_node->ty->dump();
									assert(false);
								}

								mergeNode(node, cur_node);
								cur_node = node;
								freed_nodes.clear();
							}
						//} else if (arg && !cur_node->findAlias(arg)) {
						} else {
							assert(!cur_node->findAlias(arg));
							assert(arg->getType() == cur_node->ty);

							AOSAlias *new_alias = new AOSAlias(arg, cur_node);
							cur_node->addAlias(new_alias);
							//errs() << "push_front!\n-- ";
							//arg->dump();
							work_list.push_front(new_alias);
							value_map[arg] = cur_node;
						}
					}

					break;
				}
				case Instruction::Store:
				{
					auto pSI = dyn_cast<StoreInst>(pI);
					//errs() << "Handle StoreInst!\n-- ";
					//pI->dump();

					if (pSI->getValueOperand() == pV) {
						//errs() << "Handle valueOp!\n-- ";
						auto ptrOp = pSI->getPointerOperand();

						if (AOSNode *node = value_map[ptrOp]) {
							if (AOSNode *mem_user = node->mem_user) {
								if (mem_user != cur_node) {
									//printNode(node);

						//errs() << "Handle valueOp!\n-- ";
									mergeNode(mem_user, cur_node);
									cur_node = mem_user;
									freed_nodes.clear();
								}
							//} else if (node != cur_node) {
							} else {
								assert(node != cur_node);

								cur_node->addMemEdge(node);
								node->setMemUser(cur_node);
							}
						}
					} else if (pSI->getPointerOperand() == pV) {
						//errs() << "Handle ptrOp!\n-- ";
						auto valOp = pSI->getValueOperand();

						if (!valOp->getType()->isPointerTy())
							break;

						if (AOSNode *node = value_map[valOp]) {
							if (AOSNode *mem_user = cur_node->mem_user) {
								if (mem_user != node) {
									mergeNode(mem_user, node);
									freed_nodes.clear();
								}
							} else {
							//} else if (node != cur_node) {
								assert(node != cur_node);

								node->addMemEdge(cur_node);
								cur_node->setMemUser(node);
							}
						}
					}

					break;
				}
				case Instruction::Load:
				{
					LoadInst *pLI = dyn_cast<LoadInst>(pI);

					if (!pLI->getType()->isPointerTy())
						break;

					//errs() << "Handle LoadInst!\n-- ";
					//pI->dump();
					if (AOSNode *node = value_map[pLI]) {
            printNode(node);
						assert(cur_node->mem_user == node);
					} else {
						if (AOSNode *mem_user = cur_node->mem_user) {
							//errs() << "cur_node->mem_user!\n-- ";
							// Add alias to mem_user
							assert(pLI->getType() == mem_user->ty);

							AOSAlias *new_alias = new AOSAlias(pLI, mem_user);
							mem_user->addAlias(new_alias);
							work_list.push_front(new_alias);
							//errs() << "push_front!\n-- ";
							//pLI->dump();
							value_map[pLI] = mem_user;
						} else {
							//errs() << "else!\n-- ";
							// Add a new node and set mem_user
							AOSNode *new_node = new AOSNode();
							AOSAlias *new_alias = new AOSAlias(pLI, new_node);

							new_node->setType(pLI->getType());
							new_node->addAlias(new_alias);
							work_list.push_front(new_alias);
							value_map[pLI] = new_node;

							new_node->addMemEdge(cur_node);
							cur_node->setMemUser(new_node);
						}
					}

					break;
				}
				case Instruction::Ret:
				{
					//errs() << "Handle RetInst!\n-- ";
					//pI->dump();
					ReturnInst *pRI = dyn_cast<ReturnInst>(pI);

					if (pRI->getReturnValue() == pV) {
						// this considers Invoke too
						for (auto pUb: pRI->getFunction()->users()) {
							if (Instruction *pIb = dyn_cast<Instruction>(pUb)) {
								Function *pF = pIb->getFunction();

								// To avoid the situation where CallInst doesn't use the return value
								if (pF && pIb->getType() == pRI->getReturnValue()->getType()) {
									//errs() << "Function name: " << pF->getName() << "\n";

									if (AOSNode *node = value_map[pIb]) {
										if (cur_node != node) {
											mergeNode(node, cur_node);
											cur_node = node;
											freed_nodes.clear();
										}
									} else {
										assert(!cur_node->findAlias(pIb));
										assert(pIb->getType() == cur_node->ty);

										AOSAlias *new_alias = new AOSAlias(pIb, cur_node);
										cur_node->addAlias(new_alias);
										//errs() << "push_front!\n-- ";
										//pIb->dump();
										work_list.push_front(new_alias);
										value_map[pIb] = cur_node;
									}
								}
							}
						}
					}

					break;
				}
				case Instruction::BitCast:
				{
					//errs() << "Handle BitCastInst!\n-- ";
					//pI->dump();
					BitCastOperator *BCOp = dyn_cast<BitCastOperator>(pI);
					AOSNode *node = cur_node;

					if (value_map[BCOp])
						break;

					if (GetElementPtrInst *pGEP = dyn_cast<GetElementPtrInst>(BCOp->getOperand(0))) {
						// Not interested in GEPInst... but GEPOp
					} else if (GEPOperator *GEPOp = dyn_cast<GEPOperator>(BCOp->getOperand(0))) {
						BCOp->dump();
						assert(false);
						//list<Value *> new_indices;

						//if (GEPOp->getPointerOperand() != pV)
						//	break;

						//for (auto it = GEPOp->idx_begin(); it != GEPOp->idx_end(); it++) {
						//	if (it != GEPOp->idx_begin())
						//		new_indices.push_back(*it);
						//}

						//if (AOSNode *nodeA = cur_node->findNodeWithTy(GEPOp->getType(), new_indices)) {

						//	//errs() << "Hello (4)!\n-- ";
						//	//errs() << "Found existing node!\n";
						//	assert(GEPOp->getType() == nodeA->ty);

						//	AOSAlias *new_alias = new AOSAlias(GEPOp, nodeA);
						//	nodeA->addAlias(new_alias);
						//	//work_list.push_front(new_alias);
						//	//value_map[GEPOp] = nodeA;
						//	node = nodeA;
						//} else {
						//	//errs() << "Create a new node!\n";
						//	AOSNode *new_node = new AOSNode();
						//	new_node->indices = new_indices;
						//	cur_node->addChild(new_node);
						//	new_node->addParent(cur_node);
						//	new_node->setType(GEPOp->getType());

						//	AOSAlias *new_alias = new AOSAlias(GEPOp, new_node);
						//	new_node->addAlias(new_alias);
						//	//work_list.push_front(new_alias);
						//	value_map[GEPOp] = new_node;
						//	node = new_node;
						//}
					}

					//pV->dump();
					//pI->dump();
					if (AOSNode *nodeB = node->findNodeWithTy(BCOp->getType())) {
						assert(BCOp->getType() == nodeB->ty);
						assert(value_map[BCOp] == nullptr);

						AOSAlias *new_alias = new AOSAlias(BCOp, nodeB);
						nodeB->addAlias(new_alias);
						work_list.push_front(new_alias);
						value_map[BCOp] = nodeB;
					} else {
						assert(value_map[BCOp] == nullptr);

						AOSNode *new_node = new AOSNode();
						//new_node->indices = new_indices;
						node->addChild(new_node);
						new_node->addParent(node);
						new_node->setType(BCOp->getType());

						AOSAlias *new_alias = new AOSAlias(BCOp, new_node);
						new_node->addAlias(new_alias);
						work_list.push_front(new_alias);
						value_map[BCOp] = new_node;
					}

					break;
				}
				case Instruction::GetElementPtr:
				{
					//errs() << "Handle GetElementPtrInst!\n-- ";
					//pI->dump();
					GEPOperator *GEPOp = dyn_cast<GEPOperator>(pI);

					if (!GEPOp->getType()->isPointerTy() ||
								GEPOp->getPointerOperand() != pV)
						break;

					if (AOSNode *node = value_map[GEPOp])
						break;

					list<Value *> new_indices;

					for (auto it = GEPOp->idx_begin(); it != GEPOp->idx_end(); it++) {
						if (it != GEPOp->idx_begin())
							new_indices.push_back(*it);
					}

					if (AOSNode *node = cur_node->findNodeWithTy(GEPOp->getType(), new_indices)) {
						assert(value_map[GEPOp] == nullptr);

						//errs() << "Found existing node!\n";
						assert(GEPOp->getType() == node->ty);

						AOSAlias *new_alias = new AOSAlias(GEPOp, node);
						node->addAlias(new_alias);
						work_list.push_front(new_alias);
						value_map[GEPOp] = node;
					} else {
						assert(value_map[GEPOp] == nullptr);

						//errs() << "Create a new node!\n";
						AOSNode *new_node = new AOSNode();
						new_node->indices = new_indices;
						cur_node->addChild(new_node);
						new_node->addParent(cur_node);
						new_node->setType(GEPOp->getType());

						AOSAlias *new_alias = new AOSAlias(GEPOp, new_node);
						new_node->addAlias(new_alias);
						work_list.push_front(new_alias);
						value_map[GEPOp] = new_node;
					}

					break;
				}

				default:
					break;
			} // switch
		}
	}
}

Value *AOSPointerAliasPass::getArgument(Value *pI, Value *pV) {
	Function *pF;

	if (InvokeInst *pII = dyn_cast<InvokeInst>(pI))
		pF = pII->getCalledFunction();
	else if (CallInst *pCI = dyn_cast<CallInst>(pI))
		pF = pCI->getCalledFunction();
	else
		assert(false);

	if (pF && !pF->isVarArg() && !pF->isDeclaration()) {
		unsigned arg_nth = 0;

		if (InvokeInst *pII = dyn_cast<InvokeInst>(pI)) {
			for (auto arg = pII->arg_begin(); arg != pII->arg_end(); ++arg) {
				if (dyn_cast<Value>(arg) == pV)
					break;

				arg_nth++;
			}

			assert(arg_nth < pII->arg_size());
		}	else if (CallInst *pCI = dyn_cast<CallInst>(pI)) {
			for (auto arg = pCI->arg_begin(); arg != pCI->arg_end(); ++arg) {
				if (dyn_cast<Value>(arg) == pV)
					break;

				arg_nth++;
			}

			assert(arg_nth < pCI->arg_size());
		}

		assert(!pF->isVarArg()); //TODO: handle variable number of arguments

		unsigned t = 0;
		for (auto arg = pF->arg_begin(); arg != pF->arg_end(); arg++) {
			if (t++ == arg_nth)
				return arg;
		}
  //TODO can intrinsic func return pointer alias?
	} else if (pF && pF->isDeclaration() &&
        (pF->getName() == "memchr" ||
        pF->getName() == "strchr" ||
        pF->getName() == "strpbrk" ||
        pF->getName() == "strstr" ||
        pF->getName() == "strtok")) {

    if (CallInst *pCI = dyn_cast<CallInst>(pI)) {
      if (pCI->getOperand(0) == pV)
        return pCI;
    }
  }

	return nullptr;
}

void AOSPointerAliasPass::getFunctionsFromCallGraph(Module &M) {
	CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
	list<CallGraphNode *> cgn_list;
  Function *main = M.getFunction("main");
	CallGraphNode *cgn = CG[main];

	cgn_list.push_back(cgn);
	if (main)
		func_list.push_back(main);

	while (!cgn_list.empty()) {
		CallGraphNode *caller = cgn_list.front();
		cgn_list.pop_front();

		//errs() << "Caller: " << caller->getFunction()->getName() << "\n";
		for (auto it = caller->begin(); it != caller->end(); it++) {
			if (CallGraphNode *callee = it->second) {
				Function *pF = callee->getFunction();

				if (pF && !pF->isDeclaration() &&
					caller->getFunction() != callee->getFunction()) {

					if (find(func_list.begin(), func_list.end(), pF) == func_list.end()) {
						//errs() << "--Callee: " << pF->getName() << "\n";
						func_list.push_back(pF);
						cgn_list.push_back(callee);
					}
				}
			}
		}
	}

  //
	if (main)
	  uncalled_list.push_back(main);
	//// TODO need special care... cuz arg will not be examined...
	for (auto &F : M) {
		if (&F && !F.isDeclaration() &&
			find(func_list.begin(), func_list.end(), &F) == func_list.end()) {
			func_list.push_back(&F);
			uncalled_list.push_back(&F);
			//errs() << "[Late] pushing func: " << F.getName() << "\n";
		}
	}
}

void AOSPointerAliasPass::dump() {
	errs() << "Start dump!\n";

	list<AOSNode *> node_list;
  set<AOSNode *> visit_set;

	for (auto it = root_node->children.begin(); it != root_node->children.end(); it++) {
    if (visit_set.find(*it) == visit_set.end()) {
      visit_set.insert(*it);
  		node_list.push_back(*it);
    }
	}

	while (!node_list.empty()) {
		AOSNode *node = node_list.front();
		node_list.pop_front();

		errs() << "Print Aliases! isMemNode: " << (node->mem_user ? "True" : "False") << "\n";
		printNode(node);

		//if (node->mem_user) {
		//	errs() << "Print Mem User Node!\n";
		//	printNode(node->mem_user);
		//}

		for (auto it = node->children.begin(); it != node->children.end(); it++) {
      if (visit_set.find(*it) == visit_set.end()) {
        visit_set.insert(*it);
        node_list.push_back(*it);
      }
		}

		errs() << "--------------------------\n";
	}
}

void AOSPointerAliasPass::printNode(AOSNode *node) {
	//errs() << node << "\n";
	//errs() << "--Print indices!\n";
	//for (auto it = node->indices.begin(); it != node->indices.end(); it++) {
	//	(*it)->dump();
	//}

	errs() << "--Print aliases!\n";
	for (auto it = node->aliases.begin(); it != node->aliases.end(); it++) {
		it->first->dump();
	}
}

//void AOSPointerAliasPass::mergeNode(AOSNode *dst, AOSNode *src) {
//	if (src == dst)
//		return;
//
//	//errs() << "Start merge!\n";
//	assert(dst != root_node);
//	assert(src != root_node);
//
//	if (freed_nodes.find(dst) != freed_nodes.end())
//		return;
//
//	if (freed_nodes.find(src) != freed_nodes.end())
//		return;
//
//	assert(dst->ty == src->ty);
//
//		//errs() << "Print dst!\n";
//		//printNode(dst);
//		//errs() << "Print src!\n";
//		//printNode(src);
//
//	//if (dst->ty != src->ty) {
//	//	errs() << "Print dst!\n";
//	//	printNode(dst);
//	//	errs() << "Print src!\n";
//	//	printNode(src);
//	//	assert(false);
//	//}
//
//	list<pair<AOSNode *, AOSNode *>> my_list;
//
//	//errs() << "Handle children!\n";
//	for (auto it = src->children.begin(); it != src->children.end(); it++) {
//		if (AOSNode *child_node = (*it)) {
//			assert(child_node != src);
//
//			// Type + indices aware merge
//			if (child_node != dst) {
//				Type *ty = child_node->ty;
//				list<Value *> indices = child_node->indices;
//
//				if (AOSNode *node = dst->findNodeWithTy(ty, indices)) {
//					if (node != child_node && node != dst) {
//						assert(node != src);
//
//						//pair<AOSNode *, AOSNode *> my_pair(node, child_node);
//						pair<AOSNode *, AOSNode *> my_pair(child_node, node);
//						my_list.push_back(my_pair);
//					}
//				}
//
//				child_node->addParent(dst);
//				dst->addChild(child_node);
//			}
//
//			child_node->removeParent(src);
//		}
//	}
//
//	//errs() << "Handle parents!\n";
//	for (auto it = src->parents.begin(); it != src->parents.end(); it++) {
//		if (AOSNode *parent_node = (*it)) {
//			assert(parent_node != src);
//
//			parent_node->removeChild(src);
//			if (parent_node != dst) {
//				parent_node->addChild(dst);
//				dst->addParent(parent_node);
//			}
//		}
//	}
//
//	//errs() << "Handle aliases!\n";
//	for (auto it = src->aliases.begin(); it != src->aliases.end(); it++) {
//		if (AOSAlias *alias = it->second) {
//			value_map[alias->ptr] = dst;
//			//alias->ptr->dump();
//			alias->setNode(dst);
//			dst->addAlias(alias);
//		}
//	}
//
//	//errs() << "Handle mem_edges!\n";
//	// add mem edges + change mem user of mem edges
//	for (auto it = src->mem_edges.begin(); it != src->mem_edges.end(); it++) {
//		if (AOSNode *mem_node = (*it)) {
//			assert(mem_node != src);
//
//			if (mem_node != dst) {
//				assert(mem_node->mem_user == src);
//				mem_node->setMemUser(nullptr);
//				mem_node->setMemUser(dst);
//				dst->addMemEdge(mem_node);
//			} else {
//				dst->setMemUser(nullptr);
//			}
//		}
//	}
//
//	if (AOSNode *user_node = src->mem_user) {
//		if (user_node == dst) {
//			dst->removeMemEdge(src);
//		} else {
//			if (dst->mem_user) {
//				if (src->mem_user == dst->mem_user) {
//					user_node->removeMemEdge(src);
//				} else {
//					user_node->removeMemEdge(src);
//					mergeNode(dst->mem_user, src->mem_user);
//				}
//			} else {
//				user_node->removeMemEdge(src);
//				user_node->addMemEdge(dst);
//
//				dst->setMemUser(src->mem_user);
//			}
//		}
//	}
//
//	freed_nodes.insert(src);
//
//	delete src;
//
//	//errs() << "Hi~\n";
//	for (auto it = my_list.begin(); it != my_list.end(); it++) {
//		AOSNode *nodeA = (*it).first;
//		AOSNode *nodeB = (*it).second;
//		//errs() << "Merge two nodes!\n";
//		//errs() << "Print nodeB!\n";
//		//printNode(nodeB);
//		//errs() << "Print nodeA!\n";
//		//printNode(nodeA);
//		mergeNode(nodeA, nodeB);
//	}
//	//errs() << "Bye~\n";
//
//	//errs() << "Finished merge!\n";
//	// TODO type, indices...?
//}


void AOSPointerAliasPass::mergeNode(AOSNode *dst, AOSNode *src) {
	if (src == dst)
		return;

	if (freed_nodes.find(dst) != freed_nodes.end())
		return;

	if (freed_nodes.find(src) != freed_nodes.end())
		return;

	for (auto child : src->children) {
		assert(child != src);
		assert(child != dst);
	}

	for (auto child : dst->children) {
		assert(child != src);
		assert(child != dst);
	}

	assert(src->mem_user != dst);
	assert(dst->mem_user != src);

	for (auto edge : src->mem_edges) {
		assert(edge != src);
		assert(edge != dst);
	}

	for (auto edge : dst->mem_edges) {
		assert(edge != src);
		assert(edge != dst);
	}

	//errs() << "Start merge!\n";
	assert(dst != root_node);
	assert(src != root_node);

	assert(dst->ty == src->ty);


	//if (freed_nodes.find(dst) != freed_nodes.end()) {
	//	printNode(dst);
	//	printNode(src);
	//	assert(false);
	//}

	//if (freed_nodes.find(src) != freed_nodes.end()) {
	//	assert(false);
	//}

	//freed_nodes.insert(src);
	//freed_nodes.insert(dst);

		//errs() << "Print dst!\n";
		//printNode(dst);
		//errs() << "Print src!\n";
		//printNode(src);

	//if (dst->ty != src->ty) {
	//	errs() << "Print dst!\n";
	//	printNode(dst);
	//	errs() << "Print src!\n";
	//	printNode(src);
	//	assert(false);
	//}

	//errs() << "Handle aliases!\n";
	for (auto alias : src->aliases) {
		assert(alias.second != nullptr);

		value_map[alias.second->ptr] = dst;
		alias.second->setNode(dst);
		dst->addAlias(alias.second);
	}

	//errs() << "Handle mem_edges!\n";
	// add mem edges + change mem user of mem edges
	for (auto edge : src->mem_edges) {
		if (edge->mem_user != src) {
			printNode(edge->mem_user);
			printNode(src);
		}

		assert(edge->mem_user == src);
		edge->setMemUser(nullptr);
		edge->setMemUser(dst);
		dst->addMemEdge(edge);	
	}

	//errs() << "Handle parents!\n";
	for (auto parent : src->parents) {
		parent->removeChild(src);
		parent->addChild(dst);
		dst->addParent(parent);
	}

	//list<pair<AOSNode *, AOSNode *>> my_list;

	//errs() << "Handle children!\n";
	for (auto child : src->children) {
		//Type *ty = child->ty;
		//list<Value *> indices = child->indices;
		//if (AOSNode *node = dst->findNodeWithTy(ty, indices)) {
		//	if (node == dst) {
		//		src->ty->dump();
		//		dst->ty->dump();
		//		ty->dump();
		//	}

		//	assert(node != dst);
		//	assert(node != src);
		//	assert(child != dst);
		//	assert(child != src);
		//	//mergeNode(node, child);

		//	pair<AOSNode *, AOSNode *> my_pair(node, child);
		//	my_list.push_back(my_pair);
		//} else {
			child->addParent(dst);
			dst->addChild(child);
			child->removeParent(src);
		//}
	}

	//errs() << "Handle mem_user!\n";
	if (AOSNode *src_mem_user = src->mem_user) {
		if (AOSNode *dst_mem_user = dst->mem_user) {

			if (src_mem_user == dst_mem_user) {
				src_mem_user->removeMemEdge(src);
			} else {
				src_mem_user->removeMemEdge(src);
				assert(dst_mem_user != dst);
				assert(dst_mem_user != src);
				assert(src_mem_user != dst);
				assert(src_mem_user != src);

				mergeNode(dst_mem_user, src_mem_user);
			}
		} else {
			src_mem_user->removeMemEdge(src);
			src_mem_user->addMemEdge(dst);

			dst->setMemUser(src_mem_user);
		}
	}

	//errs() << "Hi~\n";
	//for (auto it = my_list.begin(); it != my_list.end(); it++) {
	//	AOSNode *nodeA = (*it).first;
	//	AOSNode *nodeB = (*it).second;
	//	//errs() << "Merge two nodes!\n";
	//	//errs() << "Print nodeB!\n";
	//	//printNode(nodeB);
	//	//errs() << "Print nodeA!\n";
	//	//printNode(nodeA);
	//	mergeNode(nodeA, nodeB);
	//}
	//errs() << "Bye~\n";

	//freed_nodes.insert(src);

	delete src;

	//errs() << "Finished merge!\n";
	// TODO type, indices...?
}


bool AOSPointerAliasPass::IsStructTy(Type *ty) {
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
