#include "kint_constant_range.h"
#include "range.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Support/Casting.h"
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/IR/Argument.h>
#include <llvm/IR/ConstantRange.h>
#include <llvm/IR/GlobalAlias.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <string>
#include <sys/types.h>

using namespace llvm;

std::string KintRangeAnalysisPass::getBBLabel(BasicBlock *BB) {
  std::string label;
  raw_string_ostream rso(label);
  BB->printAsOperand(rso, false);

  return label;
}

constexpr auto KintRangeAnalysisPass::cmpRegion() {
  return KintConstantRange::makeAllowedICmpRegion;
}

void KintRangeAnalysisPass::rangeAnalysis(Function &F) {
  outs() << "Range Analysis for Function: " << F.getName() << "\n";
  auto &BBRange = functionsToRangeInfo[&F];

  for (auto &BBref : F) {
    auto BB = &BBref;

    auto &sumRange = BBRange[BB];

    for (const auto &predBB : predecessors(BB)) {
      if (backEdges[BB].contains(predBB))
        continue;
      outs() << "Trying to merge" << getBBLabel(predBB) << " and "
             << getBBLabel(BB) << "\n";
      auto branchRange = BBRange[predBB];
      if (auto terminator = predBB->getTerminator();
          auto branch = dyn_cast<BranchInst>(terminator)) {
        if (branch->isConditional()) {
          if (auto cmp = dyn_cast<ICmpInst>(branch->getCondition())) {
            auto lhs = cmp->getOperand(0);
            auto rhs = cmp->getOperand(1);

            if (!lhs->getType()->isIntegerTy() ||
                !rhs->getType()->isIntegerTy()) {
              outs() << "The branch operands are not integers\n"
                     << *cmp << "\n";
            } else {
              auto lRange = getRangeByBB(lhs, predBB);
              auto rRange = getRangeByBB(rhs, predBB);
              bool isTrueBranch = branch->getSuccessor(0) == BB;
              if (isTrueBranch) {
                KintConstantRange newLRange =
                    KintRangeAnalysisPass::cmpRegion()(cmp->getPredicate(),
                                                       rRange);
                KintConstantRange newRRange =
                    KintRangeAnalysisPass::cmpRegion()(
                        cmp->getSwappedPredicate(), lRange);

                // Don't change constant's value.
                branchRange[lhs] = dyn_cast<ConstantInt>(lhs)
                                       ? lRange
                                       : lRange.intersectWith(newLRange);
                branchRange[rhs] = dyn_cast<ConstantInt>(rhs)
                                       ? rRange
                                       : rRange.intersectWith(newRRange);
              } else {
                KintConstantRange newLRange =
                    KintRangeAnalysisPass::cmpRegion()(
                        cmp->getInversePredicate(), rRange);
                KintConstantRange newRRange =
                    KintRangeAnalysisPass::cmpRegion()(
                        cmp->getInversePredicate(cmp->getPredicate()), lRange);

                branchRange[lhs] = dyn_cast<ConstantInt>(lhs)
                                       ? lRange
                                       : lRange.intersectWith(newLRange);
                branchRange[rhs] = dyn_cast<ConstantInt>(rhs)
                                       ? rRange
                                       : rRange.intersectWith(newRRange);
              }

              if (branchRange[lhs].isEmptySet() ||
                  branchRange[rhs].isEmptySet()) {
                impossibleBranches[cmp] = isTrueBranch;
              } else {
                branchRange[cmp] = KintConstantRange(APInt(1, isTrueBranch));
              }
            }
          }
        }
      } else if (auto switchInst = dyn_cast<SwitchInst>(terminator)) {
        auto cond = switchInst->getCondition();
        if (cond->getType()->isIntegerTy()) {
          auto condRange = getRangeByBB(cond, predBB);
          auto emptyRange = KintConstantRange::getEmpty(
              cond->getType()->getIntegerBitWidth());
          if (switchInst->getDefaultDest() == BB) {
            for (auto c : switchInst->cases()) {
              auto caseVal = c.getCaseValue();
              emptyRange = emptyRange.unionWith(caseVal->getValue());
            }
          } else {
            for (auto c : switchInst->cases()) {
              if (c.getCaseSuccessor() == BB) {
                auto caseVal = c.getCaseValue();
                emptyRange = emptyRange.unionWith(caseVal->getValue());
              }
            }
          }

          branchRange[cond] = condRange.intersectWith(emptyRange);
        }
      } else {
        outs() << "Unknown terminator instruction: " << *predBB->getTerminator()
               << "\n";
      }
      analyzeFunction(F, branchRange);
    }
    if (BB->isEntryBlock()) {
      outs() << "No predecessors for entry block: " << BB;
      analyzeFunction(F, sumRange);
    }
  }
}

KintConstantRange KintRangeAnalysisPass::getRangeByBB(Value *var,
                                                      BasicBlock *BB) {
  return getRange(var, functionsToRangeInfo[BB->getParent()][BB]);
}

KintConstantRange
KintRangeAnalysisPass::getRange(Value *var, const RangeMap &globalRangeMap) {
  // auto itVar = globalRangeMap.find(const_cast<Value *>(var));
  if (this->globalRangeMap.count(const_cast<Value *>(var))) {
    return globalRangeMap.at(var);
  }
  // if the var is a constant integer
  if (auto *constInt = dyn_cast<ConstantInt>(var)) {
    return KintConstantRange(constInt->getValue());
  } else {
    // if the var is a global variable
    if (auto *GV = dyn_cast<GlobalVariable>(var)) {
      return globalValueRangeMap[GV];
    }
  }
  // if the var type is unknown
  errs() << "Unknown Operand Type: " << *var << "\n";
  return KintConstantRange(var->getType()->getIntegerBitWidth(), true);
}

KintConstantRange
KintRangeAnalysisPass::handleCallInst(CallInst *call, RangeMap &globalRangeMap,
                                      Instruction &I) {
  KintConstantRange outputRange;
  if (const auto f = call->getCalledFunction()) {
    if (sinkedFunctions.contains(f->getName())) {
      // Iterate through the arguments of the sinked function
      for (const auto &arg : f->args()) {
        const size_t argIndex = arg.getArgNo();

        // Check if the argument is an interger type
        if (arg.getType()->isIntegerTy()) {
          // Update the gloabalrangemap for the argument by taking the union of
          // the current range and the range of the corresponding argument in
          // the call instruction
          globalRangeMap[const_cast<Argument *>(&arg)] =
              getRange(call->getArgOperand(argIndex), globalRangeMap)
                  .unionWith(
                      getRange(const_cast<Argument *>(&arg), globalRangeMap));
          // Update the function range map for the correpsonding called function
          functionReturnRangeMap[functionsToTaintSources[f][argIndex]
                                     ->getCalledFunction()] =
              globalRangeMap[const_cast<Argument *>(&arg)];
        }
      }
    } else {
      for (const auto &arg : f->args()) {
        if (arg.getType()->isIntegerTy()) {
          globalRangeMap[const_cast<Argument *>(&arg)] =
              getRange(call->getArgOperand(arg.getArgNo()), globalRangeMap)
                  .unionWith(
                      getRange(const_cast<Argument *>(&arg), globalRangeMap));
        }
      }
    }

    if (f->getReturnType()->isIntegerTy()) {
      return functionReturnRangeMap[f];
    }
  }
  return KintConstantRange(I.getType()->getIntegerBitWidth(), true);
}

void KintRangeAnalysisPass::handleStoreInst(StoreInst *store,
                                            RangeMap &globalRangeMap,
                                            Instruction &I) {
  // Check the range being stored is of the interger type
  KintRangeAnalysisPass rangeAnalysis;
  const auto val = store->getValueOperand();
  const auto ptr = store->getPointerOperand();

  if (!val->getType()->isIntegerTy()) {
    return;
  }

  auto valRange = getRange(val, globalRangeMap);
  if (const auto GV = dyn_cast<GlobalVariable>(ptr)) {
    globalValueRangeMap[GV] = globalValueRangeMap[GV].unionWith(valRange);
  } else if (const auto gep = dyn_cast<GetElementPtrInst>(ptr)) {
    auto gepAddr = gep->getPointerOperand();

    if (auto *gepGV = dyn_cast<GlobalVariable>(gepAddr)) {
      auto itGepGV = globalRangeMap.find(gepGV);
      if (gep->getNumIndices() == 2 && itGepGV != globalRangeMap.end()) {
        auto index = gep->getOperand(2);
        auto itIndex = globalRangeMap.find(index);
        if (itIndex != globalRangeMap.end()) {
          auto indexRange = itIndex->second;
          auto arrayRange = itGepGV->second;
          auto arraySize = arrayRange.getUnsignedMax().getLimitedValue();
          auto indexSize = indexRange.getUnsignedMax().getLimitedValue();

          if (indexSize >= arraySize) {
            rangeAnalysis.gepOutOfRange.insert(gep);
          }

          for (uint64_t i = indexRange.getLower().getLimitedValue();
               i < std::min(arraySize, indexSize); i++) {
            auto newIndexRange = KintConstantRange(APInt(32, i));
            auto newArrayRange = itGepGV->second;
            auto newRange = newArrayRange.unionWith(newIndexRange);
            globalRangeMap.emplace(gep, newRange);
          }
        }
      }
    }
  }
}

void KintRangeAnalysisPass::handleReturnInst(ReturnInst *ret,
                                             RangeMap &globalRangeMap,
                                             Instruction &I) {
  Function *F = ret->getFunction();
  if (F->getReturnType()->isIntegerTy()) {
    functionReturnRangeMap[F] = getRange(ret->getReturnValue(), globalRangeMap)
                                    .unionWith(functionReturnRangeMap[F]);
  }
}

KintConstantRange KintRangeAnalysisPass::handleSelectInst(
    SelectInst *operand, RangeMap &globalRangeMap, Instruction &I) {
  outs() << "Found select instruction: " << operand->getOpcodeName() << "\n";
  // Add a full range for the current instruction to the global range map
  globalRangeMap.emplace(
      &I, KintConstantRange::getFull(operand->getType()->getIntegerBitWidth()));
  // Get the true and false values from the SelectInst
  KintConstantRange trueRange = globalRangeMap.at(operand->getTrueValue());
  KintConstantRange falseRange = globalRangeMap.at(operand->getFalseValue());
  // Compute the new range by taking the union of the true and false value
  // ranges
  KintConstantRange outputRange = trueRange.unionWith(falseRange);
  // Update the global range map with the computed range
  return outputRange;
}

KintConstantRange KintRangeAnalysisPass::handleCastInst(
    CastInst *operand, RangeMap &globalRangeMap, Instruction &I) {
  outs() << "Found cast instruction: " << operand->getOpcodeName() << "\n";
  KintConstantRange outputRange = KintConstantRange();
  // Get the input operand of the cast instruction
  auto inp = operand->getOperand(0);
  // If the input operand is not an integer type, set the range to the full bit
  // width
  if (operand->getType()->isIntegerTy()) {
    if (!inp->getType()->isIntegerTy()) {
      outputRange =
          KintConstantRange(operand->getType()->getIntegerBitWidth(), true);
    } else {
      // If the input is an integer type, compute the output range based on the
      // cast operation
      auto itInp = globalRangeMap.find(inp);
      if (itInp != globalRangeMap.end()) {
        auto inpRange = itInp->second;
        const uint32_t bits = operand->getType()->getIntegerBitWidth();
        switch (operand->getOpcode()) {
        case CastInst::Trunc:
          outputRange = inpRange.truncate(bits);
          break;
        case CastInst::ZExt:
          outputRange = inpRange.zeroExtend(bits);
          break;
        case CastInst::SExt:
          outputRange = inpRange.signExtend(bits);
          break;
        default:
          outs() << "Unsupported cast instruction: " << operand->getOpcodeName()
                 << "\n";
          return inpRange;
        }
      }
    }
  }
  return outputRange;
}

KintConstantRange KintRangeAnalysisPass::handlePHINode(PHINode *operand,
                                                       RangeMap &globalRangeMap,
                                                       Instruction &I) {
  outs() << "Found phi node: " << operand->getOpcodeName() << "\n";
  KintConstantRange outputRange =
      KintConstantRange::getEmpty(operand->getType()->getIntegerBitWidth());
  globalRangeMap.emplace(operand, outputRange);
  // Iterate through the incoming values of the PHI node
  for (unsigned i = 0; i < operand->getNumIncomingValues(); i++) {
    // Get the range of the incoming value
    auto IB = operand->getIncomingBlock(i);
    if (backEdges[I.getParent()].contains(IB)) {
      continue;
    }
    auto IV = operand->getIncomingValue(i);
    KintConstantRange incomingRange = getRangeByBB(IV, IB);
    // Union the incoming range with the PHI node range
    outputRange = outputRange.unionWith(incomingRange);
  }
  // Update the global range map with the computed range
  return outputRange;
}

void KintRangeAnalysisPass::handleLoadInst(LoadInst *operand,
                                           RangeMap &globalRangeMap,
                                           Instruction &I) {
  outs() << "Found load instruction: " << operand->getOpcodeName() << "\n";
  // Get the pointer operand of the load instruction
  auto ptrAddr = operand->getPointerOperand();
  // If the address is a GlobalVariable, it retrieves the range associated
  // with the global variable and assigns it to globalRangeMap.
  if (auto *GV = dyn_cast<GlobalVariable>(ptrAddr)) {
    if (GV->hasInitializer()) {
      if (auto *CI = dyn_cast<ConstantInt>(GV->getInitializer())) {
        globalRangeMap.emplace(&I, KintConstantRange(CI->getValue()));
      }
    }
  } else if (auto gep = dyn_cast<GetElementPtrInst>(ptrAddr)) {
    KintRangeAnalysisPass rangeAnalysis;
    bool isInRange = false;
    auto gepAddr = gep->getPointerOperand();
    // If the base address is a GlobalVariable, it means we are accessing a
    // global array
    if (auto *gepGV = dyn_cast<GlobalVariable>(gepAddr)) {
      // checks if the array is one-dimensional and if it has associated range
      // information
      auto itGepGV = globalRangeMap.find(gepGV);
      if (gep->getNumIndices() == 2 && itGepGV != globalRangeMap.end()) {
        // retrieves the index being accessed in the array and calculates the
        // size of the array and the range of the index.
        auto index = gep->getOperand(2);
        auto itIndex = globalRangeMap.find(index);
        if (itIndex != globalRangeMap.end()) {
          auto indexRange = itIndex->second;
          auto arrayRange = itGepGV->second;
          auto arraySize = arrayRange.getUpper().getLimitedValue();
          auto indexSize = indexRange.getUpper().getLimitedValue();
          // If the index is out of bounds, it inserts the GEP instruction into
          // the gepOutOfRange set.
          if (indexSize >= arraySize) {
            rangeAnalysis.gepOutOfRange.insert(gep);
          }

          // iterates through the valid index range and calculates the union of
          // the associated ranges, and updateing the global range map.
          for (uint64_t i = indexRange.getLower().getLimitedValue();
               i < std::min(arraySize, indexSize); i++) {
            auto newIndexRange = ConstantRange(APInt(32, i));
            auto newArrayRange = itGepGV->second;
            auto newRange = newArrayRange.unionWith(newIndexRange);
            globalRangeMap.emplace(gep, newRange);
          }
          isInRange = true;
        }
      }
    }
    if (!isInRange) {
      // If the GEP operation was not successfully handled, a warning is
      // printed,
      //  and global map range is set to the full range.
      outs() << "WARNING: GEP operation was not successfully handled: " << *gep
             << "\n";
    }
  } else {
    // If the address is not a GlobalVariable, a warning is printed,
    // and global map range is set to the full range.
    outs() << "WARNING: Unknown address to load: " << *ptrAddr << "\n";
  }
}

// compute the range for a given BinaryOperator instruction
KintConstantRange KintRangeAnalysisPass::computeBinaryOperatorRange(
    BinaryOperator *&BO, const RangeMap &globalRangeMap) {
  auto lhs = BO->getOperand(0);
  auto rhs = BO->getOperand(1);

  auto lhsRange = getRange(lhs, globalRangeMap);
  auto rhsRange = getRange(rhs, globalRangeMap);

  switch (BO->getOpcode()) {
  case Instruction::Add:
    return lhsRange.add(rhsRange);
  case Instruction::Sub:
    return lhsRange.sub(rhsRange);
  case Instruction::Mul:
    return lhsRange.multiply(rhsRange);
  case Instruction::SDiv:
    return lhsRange.sdiv(rhsRange);
  case Instruction::UDiv:
    return lhsRange.udiv(rhsRange);
  case Instruction::Shl:
    return lhsRange.shl(rhsRange);
  case Instruction::LShr:
    return lhsRange.lshr(rhsRange);
  case Instruction::AShr:
    return lhsRange.ashr(rhsRange);
  case Instruction::SRem:
    return lhsRange.srem(rhsRange);
  case Instruction::URem:
    return lhsRange.urem(rhsRange);
  case Instruction::And:
    return lhsRange.binaryAnd(rhsRange);
  case Instruction::Or:
    return lhsRange.binaryOr(rhsRange);
  case Instruction::Xor:
    return lhsRange.binaryXor(rhsRange);
  default:
    // Handle other instructions and cases as needed.
    errs() << "Unhandled binary operator: " << BO->getOpcodeName() << "\n";
    return rhsRange;
  }
}
