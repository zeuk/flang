//===--- CGExprCharacter.cpp - Emit LLVM Code for Character Exprs --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Expr nodes with character types as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "flang/AST/ASTContext.h"
#include "flang/AST/ExprVisitor.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include <string.h>

namespace flang {
namespace CodeGen {

#define MANGLE_CHAR_FUNCTION(Str, Type) \
  (Str "_char1")

class CharacterExprEmitter
  : public ConstExprVisitor<CharacterExprEmitter, CharacterValueTy> {
  CodeGenFunction &CGF;
  CGBuilderTy &Builder;
  llvm::LLVMContext &VMContext;
  CharacterValueTy Dest;
public:

  CharacterExprEmitter(CodeGenFunction &cgf);

  bool hasDestination() const {
    return Dest.Ptr != nullptr;
  }
  CharacterValueTy takeDestination() {
    auto Result = Dest;
    Dest = CharacterValueTy(nullptr, nullptr);
    return Result;
  }
  void setDestination(CharacterValueTy Value) {
    assert(Value.Ptr);
    Dest = Value;
  }

  CharacterValueTy EmitExpr(const Expr *E);
  CharacterValueTy VisitCharacterConstantExpr(const CharacterConstantExpr *E);
  CharacterValueTy VisitVarExpr(const VarExpr *E);
  CharacterValueTy VisitReturnedValueExpr(const ReturnedValueExpr *E);
  CharacterValueTy VisitBinaryExprConcat(const BinaryExpr *E);
  CharacterValueTy VisitSubstringExpr(const SubstringExpr *E);
  CharacterValueTy VisitCallExpr(const CallExpr *E);
  CharacterValueTy VisitIntrinsicCallExpr(const IntrinsicCallExpr *E);
  CharacterValueTy VisitArrayElementExpr(const ArrayElementExpr *E);
};

CharacterExprEmitter::CharacterExprEmitter(CodeGenFunction &cgf)
  : CGF(cgf), Builder(cgf.getBuilder()),
    VMContext(cgf.getLLVMContext()), Dest(nullptr, nullptr) {
}

CharacterValueTy CharacterExprEmitter::EmitExpr(const Expr *E) {
  return Visit(E);
}

CharacterValueTy CharacterExprEmitter::VisitCharacterConstantExpr(const CharacterConstantExpr *E) {
  return CharacterValueTy(Builder.CreateGlobalStringPtr(E->getValue()),
                          llvm::ConstantInt::get(CGF.getModule().SizeTy,
                                                 strlen(E->getValue())));
}

CharacterValueTy CharacterExprEmitter::VisitVarExpr(const VarExpr *E) {
  auto VD = E->getVarDecl();
  if(VD->isArgument())
    return CGF.ExtractCharacterValue(CGF.GetVarPtr(VD));
  else if(VD->isParameter())
    return EmitExpr(VD->getInit());
  return CGF.GetCharacterValueFromPtr(CGF.GetVarPtr(VD), VD->getType());
}

CharacterValueTy CharacterExprEmitter::VisitReturnedValueExpr(const ReturnedValueExpr *E) {
  return CGF.ExtractCharacterValue(CGF.GetRetVarPtr());
}

// FIXME could be optimized by folding consecutive concats to one destination
CharacterValueTy CharacterExprEmitter::VisitBinaryExprConcat(const BinaryExpr *E) {
  auto CharType = CGF.getContext().CharacterTy;
  CharacterValueTy Dest;
  if(hasDestination()) {
    Dest = takeDestination();
  } else {
    // FIXME temp size overflow checking.
    auto ExtLHS = E->getLHS()->getType().getExtQualsPtrOrNull();
    auto ExtRHS = E->getRHS()->getType().getExtQualsPtrOrNull();
    auto Size = (ExtLHS && ExtLHS->hasLengthSelector()? ExtLHS->getLengthSelector() : 1) +
                (ExtRHS && ExtRHS->hasLengthSelector()? ExtRHS->getLengthSelector() : 1);
    auto TempType = CGF.getContext().getExtQualType(CGF.getContext().CharacterTy.getTypePtr(),
                                                    Qualifiers(),
                                                    0, false, false, Size);
    Dest = CGF.GetCharacterValueFromPtr(
             CGF.CreateTempAlloca(CGF.ConvertTypeForMem(TempType), "characters"),
             TempType);
  }

  // a = b // c
  auto Src1 = EmitExpr(E->getLHS());
  auto Src2 = EmitExpr(E->getRHS());
  auto Func = CGF.getModule().GetRuntimeFunction3(MANGLE_CHAR_FUNCTION("concat", CharType),
                                                  CharType, CharType, CharType);
  CGF.EmitCall3(Func, Dest, Src1, Src2);
  return Dest;
}

CharacterValueTy CharacterExprEmitter::VisitSubstringExpr(const SubstringExpr *E) {
  auto Str = EmitExpr(E->getTarget());
  if(E->getStartingPoint()) {
    auto Start = Builder.CreateSub(CGF.EmitSizeIntExpr(E->getStartingPoint()),
                                   llvm::ConstantInt::get(CGF.getModule().SizeTy,
                                                          1));
    Str.Ptr = Builder.CreateGEP(Str.Ptr, Start);
    if(E->getEndPoint()) {
      auto End = CGF.EmitSizeIntExpr(E->getEndPoint());
      Str.Len = Builder.CreateSub(End, Start);
    } else
      Str.Len = Builder.CreateSub(Str.Len, Start);
  }
  else if(E->getEndPoint())
    Str.Len = CGF.EmitSizeIntExpr(E->getEndPoint());
  return Str;
}

CharacterValueTy CharacterExprEmitter::VisitCallExpr(const CallExpr *E) {
  CharacterValueTy Dest;
  if(hasDestination())
    Dest = takeDestination();
  else {
    // FIXME function returning CHARACTER*(*)
    auto RetType = E->getFunction()->getType();
    Dest = CGF.GetCharacterValueFromPtr(
             CGF.CreateTempAlloca(CGF.ConvertTypeForMem(RetType), "characters"),
             RetType);
  }

  CallArgList ArgList;
  ArgList.addReturnValueArg(Dest);
  return CGF.EmitCall(E->getFunction(), ArgList, E->getArguments()).asCharacter();
}

CharacterValueTy CharacterExprEmitter::VisitIntrinsicCallExpr(const IntrinsicCallExpr *E) {
  return CGF.EmitIntrinsicCall(E).asCharacter();
}

CharacterValueTy CharacterExprEmitter::VisitArrayElementExpr(const ArrayElementExpr *E) {
  return CGF.EmitArrayElementExpr(E).asCharacter();
}

void CodeGenFunction::EmitCharacterAssignment(const Expr *LHS, const Expr *RHS) {
  auto CharType = getContext().CharacterTy;
  auto Dest = EmitCharacterExpr(LHS);
  CharacterExprEmitter EV(*this);
  EV.setDestination(Dest);
  auto Src = EV.EmitExpr(RHS);

  if(EV.hasDestination()) {
    auto Func = CGM.GetRuntimeFunction2(MANGLE_CHAR_FUNCTION("assignment", CharType),
                                        CharType, CharType);
    EmitCall2(Func, Dest, Src);
  }
}

llvm::Value *CodeGenFunction::GetCharacterTypeLength(QualType T) {
  llvm::ConstantInt::get(CGM.SizeTy,
                         getTypes().GetCharacterTypeLength(T));
}

CharacterValueTy CodeGenFunction::GetCharacterValueFromPtr(llvm::Value *Ptr,
                                                           QualType StorageType) {
  return CharacterValueTy(Builder.CreateConstInBoundsGEP2_32(Ptr, 0, 0),
                          GetCharacterTypeLength(StorageType));
}

CharacterValueTy CodeGenFunction::EmitCharacterExpr(const Expr *E) {
  CharacterExprEmitter EV(*this);
  return EV.EmitExpr(E);
}

CharacterValueTy CodeGenFunction::ExtractCharacterValue(llvm::Value *Agg) {
  return CharacterValueTy(Builder.CreateExtractValue(Agg, 0, "ptr"),
                          Builder.CreateExtractValue(Agg, 1, "len"));
}

llvm::Value *CodeGenFunction::CreateCharacterAggregate(CharacterValueTy Value) {
  llvm::Value *Result = llvm::UndefValue::get(
                          getTypes().GetCharacterType(Value.Ptr->getType()));
  Result = Builder.CreateInsertValue(Result, Value.Ptr, 0, "ptr");
  return Builder.CreateInsertValue(Result, Value.Len, 1, "len");
}

llvm::Value *CodeGenFunction::EmitCharacterRelationalExpr(BinaryExpr::Operator Op, CharacterValueTy LHS,
                                                          CharacterValueTy RHS) {
  auto CharType = getContext().CharacterTy;
  auto Func = CGM.GetRuntimeFunction2(MANGLE_CHAR_FUNCTION("compare", CharType),
                                      CharType, CharType, CGM.Int32Ty);
  auto Result = EmitCall2(Func, LHS, RHS).asScalar();
  return ConvertComparisonResultToRelationalOp(Op, Result);
}

llvm::Value *CodeGenFunction::EmitCharacterDereference(CharacterValueTy Value) {
  return Builder.CreateLoad(Value.Ptr);
}

RValueTy CodeGenFunction::EmitIntrinsicCallCharacter(intrinsic::FunctionKind Func,
                                                     CharacterValueTy Value) {
  auto CharType = getContext().CharacterTy;
  CGFunction RuntimeFunc;
  switch(Func){
  case intrinsic::LEN:
    return EmitSizeIntToIntConversion(Value.Len);
    break;
  case intrinsic::LEN_TRIM:
    RuntimeFunc = CGM.GetRuntimeFunction1(MANGLE_CHAR_FUNCTION("lentrim", CharType),
                                          CharType, CGM.SizeTy);
    return EmitSizeIntToIntConversion(EmitCall1(RuntimeFunc, Value).asScalar());
    break;
  default:
    llvm_unreachable("invalid character intrinsic");
  }
  return EmitCall1(RuntimeFunc, Value);
}

static BinaryExpr::Operator GetLexicalComparisonOp(intrinsic::FunctionKind Func) {
  switch(Func) {
  case intrinsic::LLE:
    return BinaryExpr::LessThanEqual;
  case intrinsic::LLT:
    return BinaryExpr::LessThan;
  case intrinsic::LGE:
    return BinaryExpr::GreaterThanEqual;
  case intrinsic::LGT:
    return BinaryExpr::GreaterThan;
  }
  llvm_unreachable("invalid intrinsic function");
}

RValueTy CodeGenFunction::EmitIntrinsicCallCharacter(intrinsic::FunctionKind Func,
                                                     CharacterValueTy A1,
                                                     CharacterValueTy A2) {
  auto CharType = getContext().CharacterTy;
  CGFunction RuntimeFunc;
  switch(Func) {
  case intrinsic::INDEX: {
    auto Diff = Builder.CreatePtrDiff(A2.Ptr, A1.Ptr);
    Diff = EmitSizeIntToIntConversion(Diff);
    return Builder.CreateAdd(Diff, GetConstantOne(getContext().IntegerTy));
    break;
  }

  case intrinsic::LLE:
  case intrinsic::LLT:
  case intrinsic::LGE:
  case intrinsic::LGT:
    RuntimeFunc = CGM.GetRuntimeFunction2(MANGLE_CHAR_FUNCTION("lexcompare", CharType),
                                          CharType, CharType, CGM.Int32Ty);
    return ConvertComparisonResultToRelationalOp(GetLexicalComparisonOp(Func),
                                                 EmitCall2(RuntimeFunc, A1, A2).asScalar());
  default:
    llvm_unreachable("invalid character intrinsic");
  }
}

}
} // end namespace flang
